#!/bin/python3

# General TODO
# - Building up a literal string is probably the worst way to do it, try to build up within a BitArray

import bitstring as bs
import math

CTU_SIZE = 32
OUTPUT_WIDTH = 3840
OUTPUT_HEIGHT = 1472

OUTPUT_WIDTH /= 3
OUTPUT_HEIGHT *= 3

OUTPUT_WIDTH = int(OUTPUT_WIDTH)
OUTPUT_HEIGHT = int(OUTPUT_HEIGHT)

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
    # Remove the previous byte alignment
    ##stream.read('bits:1')
    ##numZeros = (8 - (stream.pos % 8)) if (stream.pos % 8 != 0) else 0
    ##stream.read('bits:{}'.format(numZeros))

    return nalString

def modifySPS(stream, width=OUTPUT_WIDTH, height=OUTPUT_HEIGHT):
    #s = stream.read('hex')
    #print(' '.join([s[i:i+2] for i in range(0, len(s), 2)]))
    #stream.pos = 0
    spsString = ''
    # We have to edit pic_width_in_luma_samples and pic_height_in_luma_samples
    # First, we have to advance to them.
    # Consume border (0x000001 -> 4*6 = 24 bits)
    spsString += stream.read('bits:24').bin
    # Consume NAL header
    # f(1), u(6, 6, 3)
    spsString += stream.read('bits:16').bin
    # Consume first 3 fields before profile_tier_level
    # u(4, 3, 1)
    spsString += stream.read('bits:8').bin
    # Consume profile_tier_level
    # TODO currently fixed for out_n, make general
    # Note all of the emulation_prevention_three_bytes
    # u(2, 1, 5, 32+32(0x000003), 1, 1, 1, 1, 8, 0, 0)
    spsString += stream.read('bits:84').bin
    # Consume next several until we get to pic width and height
    # TODO check for chroma_format_idc==3, assuming this is not true
    # ue(v, v)
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
    #spsHex = bs.Bits(spsString).hex
    #print(' '.join([spsHex[i:i+2] for i in range(0, len(spsHex), 2)]))
    #print()
    return bs.Bits(spsString), (width, height)


def modifyPPS(stream, num_tile_rows=3, num_tile_cols=1):
    ppsString = ''
    # Have to flip tiles_enabled_flag then insert some related fields
    # First consume up to tiles_enabled_flag
    # TODO assuming cu_qp_delta_enabled_flag is 1
    # Consume border (0x000001 -> 4*6 = 24 bits)01011101
    ppsString += stream.read('bits:24').bin
    # Consume NAL header
    # f(1), u(6, 6, 3)
    ppsString += stream.read('bits:16').bin
    # Now consume PPS data until tiles_enabled_flag
    ppsString += bs.Bits(ue=stream.read('ue')).bin
    ppsString += bs.Bits(ue=stream.read('ue')).bin
    ppsString += stream.read('bits:7').bin
    ppsString += bs.Bits(ue=stream.read('ue')).bin
    ppsString += bs.Bits(ue=stream.read('ue')).bin
    ppsString += bs.Bits(ue=stream.read('se')).bin
    ppsString += stream.read('bits:1').bin
    # Set transform_skip_enabled_flag and cu_qp_delta_enabled_flag to 0
    # TODO really need?
    #stream.read('bits:2')
    #ppsString += '00'
    ppsString += stream.read('bits:2').bin
    # Discard the diff_cu_qp_delta_depth since we don't need it
    #stream.read('ue')
    # EDIT: do not discard, it's below
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
    # num_tile_columns_minus1 and num_tile_rows_minus1, both ue(v)
    ppsString += bs.Bits(ue=num_tile_cols-1).bin
    ppsString += bs.Bits(ue=num_tile_rows-1).bin
    # uniform_spacing_flag and loop_filter_across_tiles_enabled_flag
    ppsString += '10'
    # pps_loop_filter_across_slices_enabled_flag
    #stream.read('bits:1')
    #ppsString += '0'
    ppsString += stream.read('bits:1').bin
    # Now copy over the rest of the bits
    ppsString = consumeNALRemainder(stream, ppsString)
    ppsString = '0b' + ppsString
    #ppsString += stream.read('bits:{}'.format(stream.len-stream.pos)).bin
    #ppsString += '0' * (8 - (len(ppsString) % 8))
    return bs.Bits(ppsString)

def modifyIFrame(stream, isFirst, segmentAddress, ctuOffsetBitSize):
    #print(stream.len)
    iString = ''
    # Have to flip first_slice_segment_in_pic_flag if not the first tile, and insert
    # slice_segment_address
    # Consume border (0x000001 -> 4*6 = 24 bits)01011101
    iString += stream.read('bits:24').bin
    # Consume NAL header
    # f(1), u(6, 6, 3)
    iString += stream.read('bits:16').bin
    # First data bit is first_slice_segment_in_pic_flag
    # Don't need to flip because it'll already be like that
    #stream.read('bits:1')
    #iString += '1' if isFirst else '0' iString += stream.read('bits:1').bin
    iString += stream.read('bits:1').bin
    # Navigate to slice_segment_address insertion spot
    # u(1), ue(v)
    # TODO assuming we don't output no_output_of_prior_pics_flag
    # TODO assuming dependent_slice_segments_enabled_flag == 0
    iString += stream.read('bits:1').bin
    iString += bs.Bits(ue=stream.read('ue')).bin
    # Modify slice_segment_address
    if not isFirst:
        iString += stream.read('bits:{}'.format(ctuOffsetBitSize)).bin
        #stream.read('bits:{}'.format(ctuOffsetBitSize))
        #iString += bs.Bits(uint=segmentAddress, length=ctuOffsetBitSize).bin
    # Navigate to num_entrypoint_offsets
    # u(0), ue(v), u(1), u(1), se(v)
    # TODO making a ton of assumptions here
    iString += bs.Bits(ue=stream.read('ue')).bin
    iString += stream.read('bits:2').bin
    iString += bs.Bits(se=stream.read('se')).bin
    # slice_loop_filter_across...
    stream.read('bits:1')
    iString += '0'
    # Insert num_entry_point_offsets (Exp-Golomb)
    # TODO make sure 0 is always ok
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
    #print(iString)
    iString = consumeNALRemainder(stream, iString, False)
    iString = '0b' + iString
    #iString += stream.read('bits:{}'.format(stream.len-stream.pos)).bin
    #iString += '0' * (8 - (len(iString) % 8))
    return bs.Bits(iString)

def modifyPFrame(stream, isFirst, segmentAddress, ctuOffsetBitSize):
    pString = ''
    # Have to flip first_slice_segment_in_pic_flag if not the first tile, insert
    # slice_segment_address, and insert num_entry_point_offsets
    # Consume border (0x000001 -> 4*6 = 24 bits)01011101
    pString += stream.read('bits:24').bin
    # Consume NAL header
    # f(1), u(6, 6, 3)
    pString += stream.read('bits:16').bin
    # First data bit is first_slice_segment_in_pic_flag
    # Don't need to set any more, already done
    #stream.read('bits:1').bin
    #pString += '1' if isFirst else '0'
    pString += stream.read('bits:1').bin
    # Navigate to slice_segment_address insertion spot
    # ue(v)
    # TODO assuming we DON'T output no_output_of_prior_pics_flag
    # TODO assuming dependent_slice_segments_enabled_flag == 0
    pString += bs.Bits(ue=stream.read('ue')).bin
    # Insert slice_segment_address
    if not isFirst:
        pString += stream.read('bits:{}'.format(ctuOffsetBitSize)).bin
        #stream.read('bits:{}'.format(ctuOffsetBitSize))
        #pString += bs.Bits(uint=segmentAddress, length=ctuOffsetBitSize).bin
    # Navigate to num_entrypoint_offsets
    pString += bs.Bits(ue=stream.read('ue')).bin # slice_type
    pString += stream.read('bits:8').bin # slice_pic_order_cnt_lsb (TODO assuming log2_max_pic_order_cnt... == 4)
    pString += stream.read('bits:1').bin # short_term_ref_... assuming 0
    pString += stream.read('bits:1').bin # slice_sao_luma_flag
    pString += stream.read('bits:1').bin # slice_sao_chroma_flag
    pString += stream.read('bits:1').bin # num_ref_idx_active_override_flag
    pString += stream.read('bits:1').bin # cabac_init_flag
    pString += bs.Bits(ue=stream.read('ue')).bin # five_minus_max_num_...
    pString += bs.Bits(se=stream.read('se')).bin # slice_qp_delta
    #pString += stream.read('bits:1').bin # slice_loop_filter_across_...
    stream.read('bits:1')
    pString += '0'
    # Insert num_entry_point_offsets (Exp-Golomb)
    # TODO make sure 0 is always ok
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
    #pString += stream.read('bits:{}'.format(stream.len-stream.pos)).bin
    #pString += '0' * (8 - (len(pString) % 8))
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
        # TODO do we need to modify SEI? What is it, really?
        getNAL(files[0]).tofile(f)
        # Advance the rest of the streams, discarding their PS information
        ##for i in range(1, NUM_FILES):
        ##    getNAL(files[i])
        ##    _, tileSizes[i] = modifySPS(getNAL(files[i]))
        ##    getNAL(files[i])
        ##    getNAL(files[i])

        # Calculate slice segment addresses
        # TODO hardcoded; instead infer
        # TODO currently assumes a 1x3 layout. Can probably automate by finding most equal factor
        # pair for a given number of tiles
        #tileCTUOffsets = [0, 120, 240]
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

        # Output the final file
        # TODO output to file during process to avoid memory issues with large files
        #outputStream.tofile(open('stitched.hevc', 'wb'))
        # Process the NALs in parallel
        #while True:
        #    for i in range(NUM_FILES):
            
