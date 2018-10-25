#include <fstream>
#include <sstream>
#include <iterator>
#include <iostream>
#include <cstring>
#include <vector>
#include <map>
#include <math.h>
#include <boost/dynamic_bitset.hpp>

#include "link_stitcher.h"

typedef unsigned char Block;
typedef boost::dynamic_bitset<Block> Bitset;

enum NALType {
	P_SLICE = 0,
	I_SLICE,
	VPS,
	SPS,
	PPS,
	SEI,
	OTHER
};

std::map<int, Bitset> ctuOffsetBits;

/**
 * Given a NAL, get its type.
 * The type is dictated by the first 6 bits after the (3-byte + 1-bit) border.
 * Keep it simple with bitmasks, no need to use Bitsets.
 */
NALType getNALType(std::vector<Block>* nal) {
	NALType nalType;
	int typeBitsOffset = 3;
	// Move past any extra 0x00's. This is mostly for VPS.
	int i = 0;
	while ((*nal)[i+2] != 0x01) {
		typeBitsOffset++;
		i++;
	}
	unsigned char typeBits = (*nal)[typeBitsOffset] >> 1;
	switch (typeBits) {
		case 0x00:
		case 0x01:
			nalType = P_SLICE;
			break;
		case 0x13:
		case 0x14:
			nalType = I_SLICE;
			break;
		case 0x20:
			nalType = VPS;
			break;
		case 0x21:
			nalType = SPS;
			break;
		case 0x22:
			nalType = PPS;
			break;
		case 0x27:
		case 0x28:
			nalType = SEI;
			break;
		default:
			nalType = OTHER;
			break;
	}
	return nalType;
}

/**
 * Given an input file stream and a buffer, take the next NAL in the stream and
 * place its raw bytes in the buffer. This includes the first border, but not
 * the second, in the buffer.
 *
 * Returns the NAL type, or -1 if there are no more NALs in the stream.
 */
int getNextNAL(unsigned char* bytes, std::vector<Block>* buf, int* bytesPos, int bytesSize) {
	if (*bytesPos >= bytesSize) {
		return -1;
	}
	// Go past the first border and consume it.
	int zeroCounter = 0;
	unsigned char c = 0xFF;
	while ((zeroCounter < 2 || c != 0x01) && *bytesPos < bytesSize) {
		c = bytes[*bytesPos];
		buf->push_back(c);
		(*bytesPos)++;
		if (c == 0x00) {
			zeroCounter++;
		}
	}
	// Stop when we encounter the next border. Do not consume it.
	zeroCounter = 0;
	while ((zeroCounter < 2 || c != 0x01) && *bytesPos < bytesSize) {
		c = bytes[*bytesPos];
		buf->push_back(c);
		(*bytesPos)++;
		if (c == 0x00) {
			zeroCounter++;
		}
		else if (zeroCounter < 2 || c != 0x01) {
			zeroCounter = 0;
		}
	}
	if (zeroCounter >= 2 && (unsigned char)c == 0x01) {
		(*bytesPos) -= 3;
		buf->pop_back();
		buf->pop_back();
		buf->pop_back();
	}
	return getNALType(buf);
}

/**
 * Copy numBits bits from oldBits into newBits, starting at oldBitsPos, by
 * appending each bit to newBits.
 *
 * Returns the number of appended bits.
 */
int copyBits(Bitset* oldBits, Bitset* newBits, int oldBitsPos, int numBits) {
	for (int i=0; i<numBits; i++) {
		newBits->push_back((*oldBits)[oldBitsPos+i]);
	}
	return numBits;
}

/**
 * Copy an Exponential Golomb-coded number from oldBits into newBits. Note
 * that we do not infer the value of the number, so this works for either
 * signed or unsigned Exp-Golomb numbers
 *
 * Returns the number of bits used to encode the number.
 */
int copyExpGolomb(Bitset* oldBits, Bitset* newBits, int oldBitsPos) {
	int numZeros = 0;
	bool curr = (*oldBits)[oldBitsPos];
	while (curr == false) {
		if (newBits != NULL) {
			newBits->push_back((*oldBits)[oldBitsPos+numZeros]);
		}
		numZeros++;
		curr = (*oldBits)[oldBitsPos+numZeros];
	}
	if (newBits != NULL) {
		for (int i=0; i<numZeros+1; i++) {
			newBits->push_back((*oldBits)[oldBitsPos+numZeros+i]);
		}
	}
	return (numZeros*2)+1;
}

int writeUnsExpGolomb(Bitset* bits, unsigned int number) {
	if (number == 0) {
		bits->push_back(1);
		return 1;
	}
	number++;
	Bitset ue(sizeof(unsigned int)*8, number);
	int firstOneIdx = ue.size() - 1;
	while (ue[firstOneIdx] != 1 && firstOneIdx >= 0) {
		firstOneIdx--;
	}
	for (int i=0; i<firstOneIdx; i++) {
		bits->push_back(0);
	}
	for (int i=0; i<=firstOneIdx; i++) {
		bits->push_back(ue[firstOneIdx-i]);
	}
	return (firstOneIdx * 2) + 1;
}

void nalToBitset(Bitset* bits, std::vector<Block>* nal) {
	// Load the NAL into a bitset
	boost::from_block_range(nal->begin(), nal->end(), *bits);
	// Reverse the order of each byte. By default, the bytes will be read in the right order,
	// but the bits within each byte will be reversed. We will reverse them again before writing
	// out, after alignment.
	Bitset tempBits(9);
	for (int i=0; i<bits->size()/8; i++) {
		for (int j=0; j<8; j++) {
			tempBits[j] = (*bits)[(i*8)+(7-j)];
		}
		for (int j=0; j<8; j++) {
			(*bits)[(i*8)+j] = tempBits[j];
		}
	}
}

void bitsetToNAL(std::vector<Block>* nal, Bitset* bits) {

	// Reverse the order of each byte again for writing out
	Bitset tempBits(9);
	for (int i=0; i<bits->size()/8; i++) {
		for (int j=0; j<8; j++) {
			tempBits[j] = (*bits)[(i*8)+(7-j)];
		}
		for (int j=0; j<8; j++) {
			(*bits)[(i*8)+j] = tempBits[j];
		}
	}
	// Place the new bits into the NAL
	nal->clear();
	boost::to_block_range(*bits, std::back_inserter(*nal));
}

void byteAlignment(Bitset* bits) {
	// Append a 1 and as many 0s as necessary to complete the byte to the end
	bits->push_back(1);
	for (int i=0; i<bits->size()%8; i++) {
		bits->push_back(0);
	}
}

void doneEditingNAL(std::vector<Block>* nal, Bitset* newBits, Bitset* oldBits, int oldBitsPos,
					bool doEmulationPrevention, bool doHeaderByteAlign) {
	// This is a convenience function that wraps up several smaller functions:
	// 1. Copy over the rest of the header byte-by-byte according to the byte alignment
	//    of the original stream. This allows us to watch for 0x03 or 0x0000, which are
	//    special cases we must be careful with.
	// 2. Perform byte alignment of the NAL header, removing the previous alignment
	// 3. Copy the NAL body as-is. We will be byte aligned and the data will be unchanged,
	//    so we do not need to watch for, e.g., 0x03 or 0x0000.
	// 4. Write the bitset back into the NAL, reversing the bits within each byte.

	// Assuming oldBitsPos is at the 1 which begins the byte_alignment
	if (doHeaderByteAlign) {
		newBits->push_back(1);
		int numZeros = (newBits->size() % 8 != 0) ? (8 - (newBits->size() % 8)) : 0;
		for (int i=0; i<numZeros; i++) {
			newBits->push_back(0);
		}
		oldBitsPos += 1;
		numZeros = (oldBitsPos % 8 != 0) ? (8 - (oldBitsPos % 8)) : 0;
		oldBitsPos += numZeros;
	}
	// First, get byte-aligned with the original stream
	int numToRead = (oldBitsPos % 8 != 0) ? (8 - (oldBitsPos % 8)) : 0;
	for (int i=0; i<numToRead; i++) {
		newBits->push_back((*oldBits)[oldBitsPos+i]);
	}
	oldBitsPos += numToRead;
	// Go through the bytes, keeping an eye out for 0x03 and 0x0000
	int idx;
	bool allZeros;
	bool isEmulationPreventionByte;
	int mostRecentByteCheck = -1;
	Bitset tempBits = Bitset(8);
	Bitset compareBits = Bitset(16);
	while (oldBitsPos < oldBits->size()) {
		// Less than 8 bits, just copy what's left
		if ((oldBits->size() - oldBitsPos) < 8) {
			numToRead = (oldBitsPos % 8 != 0) ? (8 - (oldBitsPos % 8)) : 0;
			for (int i=0; i<numToRead; i++) {
				newBits->push_back((*oldBits)[oldBitsPos+i]);
			}
		}
		// At least one full byte left
		else {
			// Get the next byte
			for (int i=0; i<8; i++) {
				tempBits[i] = (*oldBits)[oldBitsPos+i];
			}
			oldBitsPos += 8;
			// If this is 0x03, ignore it, and do not copy it. Instead, we
			// will search for 0x0000 ourselves and insert them. This is to
			// handle the difference arrangement of bits after out changes.
			compareBits.clear();
			compareBits.push_back(0);
			compareBits.push_back(0);
			compareBits.push_back(0);
			compareBits.push_back(0);
			compareBits.push_back(0);
			compareBits.push_back(0);
			compareBits.push_back(1);
			compareBits.push_back(1);
			isEmulationPreventionByte = true;
			for (int i=0; i<8; i++) {
				if (compareBits[i] != tempBits[i]) {
					isEmulationPreventionByte = false;
				}
			}
			if (doEmulationPrevention && isEmulationPreventionByte) {
				continue;
			}
			// If this is not 0x03, append it to newBits and see if we need
			// a 0x03 by checking if the most recent 2 bytes were 0x0000. If
			// so, append one.
			else {
				for (int i=0; i<8; i++) {
					newBits->push_back(tempBits[i]);
				}
				if (doEmulationPrevention && newBits->size() > 16) {
					idx = (newBits->size() - (newBits->size() % 8)) - 16;
					if (idx != mostRecentByteCheck) {
						mostRecentByteCheck = idx;
						allZeros = true;
						for (int i=0; i<16; i++) {
							if ((*newBits)[idx+i] != 0) {
								allZeros = false;
							}
						}
						if (allZeros) {
							tempBits.clear();
							int numExtra = newBits->size() % 8;
							for (int i=0; i<numExtra; i++) {
								tempBits.push_back((*newBits)[newBits->size()-1]);
								newBits->pop_back();
							}
							newBits->push_back(0);
							newBits->push_back(0);
							newBits->push_back(0);
							newBits->push_back(0);
							newBits->push_back(0);
							newBits->push_back(0);
							newBits->push_back(1);
							newBits->push_back(1);
							for (int i=0; i<tempBits.size(); i++) {
								newBits->push_back(tempBits[i]);
							}
						}
					}
				}
			}
		}
	}
	// Remove the previous byte alignment
	int lastOneIdx = newBits->size() - 1;
	while ((*newBits)[lastOneIdx] != 1) {
		lastOneIdx--;
	}
	int numToRemove = newBits->size() - lastOneIdx;
	for (int i=0; i<numToRemove; i++) {
		newBits->pop_back();
	}
	// Add our own byte alignment
	byteAlignment(newBits);
	// Write it to the NAL
	bitsetToNAL(nal, newBits);
}

// Change pic width/height
void modifySPS(std::vector<Block>* nal) {
	int oldBitsPos = 0;
	Bitset oldBits(nal->size()*8);
	Bitset newBits(0);
	// Convert the NAL to some bits
	nalToBitset(&oldBits, nal);
	// Navigate to the right spot
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 132);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	// Consume old values for width/height, insert new ones
	oldBitsPos += copyExpGolomb(&oldBits, NULL, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, NULL, oldBitsPos);
	writeUnsExpGolomb(&newBits, 3840);
	writeUnsExpGolomb(&newBits, 1472);
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, true, false);
}

// Enable tiles and insert the tile-related fields
void modifyPPS(std::vector<Block>* nal) {
	const int NUM_TILE_COLS = 3;
	const int NUM_TILE_ROWS = 1;
	int oldBitsPos = 0;
	Bitset oldBits(nal->size()*8);
	Bitset newBits(0);
	// Convert the NAL to some bits
	nalToBitset(&oldBits, nal);
	// Navigate to the right spot
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 40);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 7);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 3);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 4);
	// Replace tiles_enabled_flag
	oldBitsPos += 1;
	newBits.push_back(1);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 1);
	// Insert tile info (num tile cols, num tile rows, 2 flags)
	writeUnsExpGolomb(&newBits, NUM_TILE_COLS-1);
	writeUnsExpGolomb(&newBits, NUM_TILE_ROWS-1);
	newBits.push_back(1);
	newBits.push_back(0);
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, true, false);
}

// TODO this is very slow, make it so we aren't creating a new bitset with each I slice
// Idea: since we know what the ctus are beforehand, just create like 2 cutOffsetBits globals
// and refer to them here based on ctuOffset. Maybe with like a map or something.
void writeCtuOffset(Bitset* bits, unsigned int ctuOffset, int ctuOffsetBitSize) {
	for (int i=0; i<ctuOffsetBitSize; i++) {
		bits->push_back(ctuOffsetBits[ctuOffset][ctuOffsetBitSize-i-1]);
	}
}

// Set segmentAddress, slice_loop_filter_across_..., num_entrypoint_offsets
void modifyISlice(std::vector<Block>* nal, bool isFirstSlice, int ctuOffset, int oldCtuOffsetBitSize,
				  int newCtuOffsetBitSize) {
	int oldBitsPos = 0;
	Bitset oldBits(nal->size()*8);
	Bitset newBits(0);
	// Convert the NAL to some bits
	nalToBitset(&oldBits, nal);
	// Navigate to the right spot and make our changes
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 42);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	if (!isFirstSlice) {
		oldBitsPos += oldCtuOffsetBitSize;
		writeCtuOffset(&newBits, ctuOffset, newCtuOffsetBitSize);
	}
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 2);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += 1;
	newBits.push_back(0);
	newBits.push_back(1);
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, false, true);
}

// Set segmentAddress, slice_loop_filter_across_..., num_entrypoint_offsets
void modifyPSlice(std::vector<Block>* nal, bool isFirstSlice, int ctuOffset, int oldCtuOffsetBitSize,
				  int newCtuOffsetBitSize) {
	int oldBitsPos = 0;
	Bitset oldBits(nal->size()*8);
	Bitset newBits(0);
	// Convert the NAL to some bits
	nalToBitset(&oldBits, nal);
	// Navigate to the right spot and make our changes
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 41);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	if (!isFirstSlice) {
		oldBitsPos += oldCtuOffsetBitSize;
		writeCtuOffset(&newBits, ctuOffset, newCtuOffsetBitSize);
	}
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 13);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += 1;
	newBits.push_back(0);
	newBits.push_back(1);
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, false, true);
}

extern "C" int doStitching(unsigned char* tiledBitstream, unsigned char* bitstream_0,
						   unsigned char* bitstream_1, int bitstream_0Size, int bitstream_1Size,
						   int* tileBitrates) {
	//tiledBitstream[0] = 0xAA;
	//tiledBitstream[1] = 0xBB;
	//tiledBitstream[2] = 0xCC;
	//tiledBitstream[3] = 0xDD;
	//tiledBitstream[4] = 0xEE;
	//return 5;

	int totalSize = 0;
	int tbPos = 0;
	int* bitstream_0Pos = (int*)malloc(sizeof(int));
	int* bitstream_1Pos = (int*)malloc(sizeof(int));
	*bitstream_0Pos = 0;
	*bitstream_1Pos = 0;
	std::vector<Block> nal;

	// Get as many NALs as we have in the stream
	const int oldCtuOffsetBitSize = ceil(log2((1280/32)*(4416/32)));
	const int newCtuOffsetBitSize = ceil(log2((3840/32)*(1472/32)));
	const int sliceSegAddrs[] = {0, 40, 80}; // 0 not used, but convenient for index
	for (int i=1; i<3; i++) {
		ctuOffsetBits.insert({sliceSegAddrs[i], Bitset(newCtuOffsetBitSize, sliceSegAddrs[i])});
	}
	int i = 0;
	int nalType;
	int ifs_idx = -1;
	while (true) {
		for (int ifs_idx=0; ifs_idx<2; ifs_idx++) {
			nalType = getNextNAL((ifs_idx == 0 ? bitstream_0 : bitstream_1), &nal,
								 (ifs_idx == 0 ? bitstream_0Pos : bitstream_1Pos),
								 (ifs_idx == 0 ? bitstream_0Size : bitstream_1Size));
			// Low qual on left and right, high in middle
			switch (nalType) {
				case P_SLICE:
					if (tileBitrates[i] == ifs_idx) {
						modifyPSlice(&nal, (i==0), sliceSegAddrs[i], oldCtuOffsetBitSize,
									 newCtuOffsetBitSize);
						std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
						totalSize += nal.size();
					}
					if (ifs_idx == 1) i++;
					break;
				case I_SLICE:
					if (tileBitrates[i] == ifs_idx) {
						modifyISlice(&nal, (i==0), sliceSegAddrs[i], oldCtuOffsetBitSize,
									 newCtuOffsetBitSize);
						std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
						totalSize += nal.size();
					}
					if (ifs_idx == 1) i++;
					break;
				case SPS:
					if (ifs_idx==0) {
						modifySPS(&nal);
						std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
						totalSize += nal.size();
					}
					break;
				case PPS:
					if (ifs_idx==0) {
						modifyPPS(&nal);
						std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
						totalSize += nal.size();
					}
					break;
				case VPS:
				case SEI:
					if (ifs_idx==0) {
						std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
						totalSize += nal.size();
					}
					if (ifs_idx == 1) i = 0;
					break;
				case OTHER:
					break;
				case -1:
					goto done;
			}
			nal.clear();
		}
	}
 done:
	free(bitstream_0Pos);
	free(bitstream_1Pos);
	return totalSize;
}
