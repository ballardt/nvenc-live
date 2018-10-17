#!/bin/python3

import bitstring as bs
import math

CTU_SIZE = 32
OUTPUT_WIDTH = 3840
OUTPUT_HEIGHT = 1472

OUTPUT_WIDTH = int(OUTPUT_WIDTH/3)
OUTPUT_HEIGHT = int(OUTPUT_HEIGHT*3)

def getNAL(stream):
    nalString = '0x'
    # Go past the first border, including it in the NAL
    while stream.peek('hex:24') != '000001':
        nalString += stream.read('hex:8')
    nalString += stream.read('hex:24')
    # Now go until the next border, leaving it for the next NAL
    while stream.peek('hex:24') != '000001':
        nalString += stream.read('hex:8')
    return bs.BitStream(nalString)

def consumeBorder(stream, peek=False):
    return stream.read('bits:24')

def checkNALType(stream):
    # The border is 24 bits, then there's the 0 bit, then 6 bits for nal_unit_type
    nalTypeBits = stream.peek('bits:31').bin[-6:]
    nalType = nalTypeBits
    if nalTypeBits in ['000000', '000001']:
        nalType = 'P_frame'
    elif nalTypeBits in ['010011', '010100']:
        nalType = 'I_frame'
    elif nalTypeBits in ['100111', '101000']:
        nalType = 'SEI'
    elif nalTypeBits in ['100000', '100001', '100010']:
        nalType = 'PS'
    return nalType

def consumeNALRemainder(stream, nalString, doEmulationPrevention=False):
    # Now get the rest
    # First, get byte-aligned (in the original stream, NOT nalString)
    numToRead = (8 - (stream.pos % 8)) if (stream.pos % 8 != 0) else 0
    nalString += stream.read('bits:{}'.format(numToRead)).bin
    # Go through the bytes, keeping an eye out for emulation_prevention_three_bytes
    zeroCounter = 0
    while stream.pos < stream.len:
        if ((stream.len - stream.pos) < 8):
            numToRead = (8 - (stream.pos % 8)) if (stream.pos % 8 != 0) else 0
            s = stream.read('bits:{}'.format(numToRead)).bin
            nalString += s
        else:
            s = stream.read('bits:8').bin
            # emulation_prevention_three_byte must be byte-aligned
            if doEmulationPrevention and s == '00000011':
                numToRead = (8 - (len(nalString) % 8)) if (len(nalString) % 8 != 0) else 0
                nalString += stream.read('bits:{}'.format(numToRead)).bin
                nalString += s
            elif doEmulationPrevention and s == '00000000':
                zeroCounter += 1
                if zeroCounter == 2:
                    numToRead = (8 - (len(nalString) % 8)) if (len(nalString) % 8 != 0) else 0
                    nalString += stream.read('bits:{}'.format(numToRead)).bin
                    nalString += s
                    nalString += '00000011'
                    zeroCounter = 0
                else:
                    nalString += s
            else:
                nalString += s

    # Remove the previous byte alignment
    nalString = nalString[:nalString.rfind('1')]
    # Byte-align by appending a 1 and then 0s
    nalString += '1'
    numZeros = (8 - (len(nalString) % 8)) if (len(nalString) % 8 != 0) else 0
    nalString += '0' * numZeros

    return nalString

# Modify SPS as follows:
# - pic_width_in_luma_samples: set to the new pixel width
# - pic_height_in_luma_samples: set to the new pixel height
def modifySPS(stream, width=OUTPUT_WIDTH, height=OUTPUT_HEIGHT):
    spsString = ''
    # Consume border (0x000001 -> 4*6 = 24 bits)
    spsString += stream.read('bits:24').bin
    # Consume NAL header
    spsString += stream.read('bits:16').bin
    # Consume first 3 fields before profile_tier_level
    spsString += stream.read('bits:8').bin
    # Consume profile_tier_level
    # TODO ensure this is valid for all NVENC output
    spsString += stream.read('bits:84').bin
    # Consume next several until we get to pic width and height
    # TODO check for chroma_format_idc==3, assuming this is not true
    spsString += bs.Bits(ue=stream.read('ue')).bin
    spsString += bs.Bits(ue=stream.read('ue')).bin
    # Now consume the width and height fields and replace them with our own
    stream.read('ue')
    stream.read('ue')
    spsString += bs.Bits(ue=width).bin
    spsString += bs.Bits(ue=height).bin
    # Now get the rest
    spsString = consumeNALRemainder(stream, spsString)
    spsString = '0b' + spsString
    return bs.Bits(spsString), (width, height)

# Modify the PPS as follows:
# - set tiles_enabled_flag = 1
# - insert the 4 tile-related fields
def modifyPPS(stream, num_tile_rows=3, num_tile_cols=1):
    ppsString = ''
    # Consume border
    ppsString += stream.read('bits:24').bin
    # Consume NAL header
    ppsString += stream.read('bits:16').bin
    # Now consume PPS data until tiles_enabled_flag
    ppsString += bs.Bits(ue=stream.read('ue')).bin
    ppsString += bs.Bits(ue=stream.read('ue')).bin
    ppsString += stream.read('bits:7').bin
    ppsString += bs.Bits(ue=stream.read('ue')).bin
    ppsString += bs.Bits(ue=stream.read('ue')).bin
    ppsString += bs.Bits(ue=stream.read('se')).bin
    ppsString += stream.read('bits:3').bin
    ppsString += bs.Bits(ue=stream.read('ue')).bin
    ppsString += bs.Bits(ue=stream.read('se')).bin
    ppsString += bs.Bits(ue=stream.read('se')).bin
    ppsString += stream.read('bits:4').bin
    # Now we're at tiles_enabled_flag, so discard it, insert a 1 instead, then consume the
    # field right after so we can insert the tile info
    stream.read('bits:1')
    ppsString += '1'
    ppsString += stream.read('bits:1').bin
    # Insert the tile info
    ppsString += bs.Bits(ue=num_tile_cols-1).bin
    ppsString += bs.Bits(ue=num_tile_rows-1).bin
    ppsString += '10'
    # pps_loop_filter_across_slices_enabled_flag
    ppsString += stream.read('bits:1').bin
    # Now copy over the rest of the bits
    ppsString = consumeNALRemainder(stream, ppsString)
    ppsString = '0b' + ppsString
    return bs.Bits(ppsString)

# Modify an I-frame as follows:
# - set slice_loop_filter_across_... = 0
# - insert num_entrypoint_offsets
def modifyIFrame(stream, isFirst, segmentAddress, ctuOffsetBitSize):
    iString = ''
    # Consume border (0x000001 -> 4*6 = 24 bits)01011101
    iString += stream.read('bits:24').bin
    # Consume NAL header
    iString += stream.read('bits:16').bin
    # Navigate to slice_loop_filter_across_...
    iString += stream.read('bits:1').bin
    iString += stream.read('bits:1').bin
    iString += bs.Bits(ue=stream.read('ue')).bin
    if not isFirst:
        iString += stream.read('bits:{}'.format(ctuOffsetBitSize)).bin
    iString += bs.Bits(ue=stream.read('ue')).bin
    iString += stream.read('bits:2').bin
    iString += bs.Bits(se=stream.read('se')).bin
    # slice_loop_filter_across_...
    stream.read('bits:1')
    iString += '0'
    # Insert num_entry_point_offsets (Exp-Golomb)
    iString += '1'
    # byte_align() the header
    iString += '1'
    numZeros = (8 - (len(iString) % 8)) if (len(iString) % 8 != 0) else 0
    iString += '0' * numZeros
    # Remove the previous byte alignment
    stream.read('bits:1')
    numZeros = (8 - (stream.pos % 8)) if (stream.pos % 8 != 0) else 0
    stream.read('bits:{}'.format(numZeros))
    # Copy the rest of the I frame
    iString = consumeNALRemainder(stream, iString, False)
    iString = '0b' + iString
    return bs.Bits(iString)

# Modify a P-frame as follows:
# - set slice_loop_filter_across_... = 0
# - insert num_entrypoint_offsets
def modifyPFrame(stream, isFirst, segmentAddress, ctuOffsetBitSize):
    pString = ''
    # Consume border (0x000001 -> 4*6 = 24 bits)01011101
    pString += stream.read('bits:24').bin
    # Consume NAL header
    pString += stream.read('bits:16').bin
    # Navigate to slice_loop_filter_across_... insertion spot
    pString += stream.read('bits:1').bin
    pString += bs.Bits(ue=stream.read('ue')).bin
    if not isFirst:
        pString += stream.read('bits:{}'.format(ctuOffsetBitSize)).bin
    pString += bs.Bits(ue=stream.read('ue')).bin # slice_type
    pString += stream.read('bits:8').bin # slice_pic_order_cnt_lsb
    pString += stream.read('bits:1').bin # short_term_ref_... assuming 0
    pString += stream.read('bits:1').bin # slice_sao_luma_flag
    pString += stream.read('bits:1').bin # slice_sao_chroma_flag
    pString += stream.read('bits:1').bin # num_ref_idx_active_override_flag
    pString += stream.read('bits:1').bin # cabac_init_flag
    pString += bs.Bits(ue=stream.read('ue')).bin # five_minus_max_num_...
    pString += bs.Bits(se=stream.read('se')).bin # slice_qp_delta
    # slice_loop_filter_across_...
    stream.read('bits:1')
    pString += '0'
    # Insert num_entry_point_offsets (Exp-Golomb)
    pString += '1'
    # byte_align() the header
    pString += '1'
    numZeros = (8 - (len(pString) % 8)) if (len(pString) % 8 != 0) else 0
    pString += '0' * numZeros
    # Remove the previous byte_alignment()
    stream.read('bits:1')
    numZeros = (8 - (stream.pos % 8)) if (stream.pos % 8 != 0) else 0
    stream.read('bits:{}'.format(numZeros))
    # Copy the rest of the P frame
    pString = consumeNALRemainder(stream, pString, False)
    pString = '0b' + pString
    return bs.Bits(pString)

if __name__=='__main__':
    NUM_FILES = 1
    NUM_SLICES = 3
    tileSizes = [-1 for _ in range(NUM_FILES)]
    with open('/home/trevor/Projects/hevc/videos/stitched.hevc', 'wb') as f:
        # Open each file
        files = [bs.BitStream(filename='/home/trevor/Projects/hevc/videos/ms9390_{}.hevc'.format(i)) for i in range(NUM_FILES)]
        # We only need PS info from one tile in the output stream header
        # VPS
        getNAL(files[0]).tofile(f)
        # SPS
        sps, tileSizes[0] = modifySPS(getNAL(files[0]), OUTPUT_WIDTH, OUTPUT_HEIGHT)
        sps.tofile(f)
        # PPS
        modifyPPS(getNAL(files[0])).tofile(f)
        # SEI
        getNAL(files[0]).tofile(f)
        # Slice segment addresses
        tileCTUOffsets = [0, 1840, 3680]
        ctuOffsetBitSize = math.ceil(math.log((OUTPUT_WIDTH/CTU_SIZE)*(OUTPUT_HEIGHT/CTU_SIZE), 2))
        # Now do I and P frames until the end
        i = 0
        while True:
            nal = getNAL(files[0])
            nalType = checkNALType(nal)
            if nalType == 'I_frame':
                print('I frame')
                modifyIFrame(nal, i==0, tileCTUOffsets[i], ctuOffsetBitSize).tofile(f)
                i+=1
            elif nalType == 'P_frame':
                print('P frame')
                modifyPFrame(nal, i==0, tileCTUOffsets[i], ctuOffsetBitSize).tofile(f)
                i+=1
            elif nalType == 'SEI':
                print('SEI')
                nal.tofile(f)
                i = 0
            elif nalType == 'PS':
                continue
            else:
                print('Error: invalid frame type "{}"'.format(nalType))
                break
