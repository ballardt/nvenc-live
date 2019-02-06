#include <fstream>
#include <sstream>
#include <iterator>
#include <iostream>
#include <iomanip>
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
NALType getNALType(std::vector<Block>& nal) {
	NALType nalType;
	int typeBitsOffset = 3;
	// Move past any extra 0x00's. This is mostly for VPS.
    // std::cerr << __LINE__ << " nal size " << nal.size() << std::endl;
	int i = 0;
	while (nal[i+2] != 0x01) {
		typeBitsOffset++;
		i++;
	}
	unsigned char typeBits = nal[typeBitsOffset] >> 1;
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
int getNextNAL(unsigned char* bytes, std::vector<Block>* buf, long* bytesPos, long bytesSize)
{
#if 0
    std::cerr << "Enter getNextNAL with " << *bytesPos << std::endl;
    unsigned char* p = bytes;
    for( int i=0; i<10; i++ )
    {
        for( int j=0; j<10; j++ )
        {
            std::cerr << std::setw(4) << int(p[i*10+j]) << " ";
        }
        std::cerr << std::endl;
    }
#endif
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
	return getNALType(*buf);
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

/**
 * Could speed this up by simply copying the Blocks for NAL Data rather than
 * appending the bits and converting to Blocks.
 */
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
		// Assume that data can be left untouched, just get the rest of it
		for (int i=0; i<oldBits->size()-oldBitsPos; i++) {
			newBits->push_back((*oldBits)[oldBitsPos+i]);
		}
	}
	else {
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
	}
	// Write it to the NAL
	bitsetToNAL(nal, newBits);
}

// Change pic width/height
void modifySPS(std::vector<Block>* nal, int width, int height) {
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
	writeUnsExpGolomb(&newBits, width);
	writeUnsExpGolomb(&newBits, height);
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, true, false);
}

// Enable tiles and insert the tile-related fields
void modifyPPS(std::vector<Block>* nal, int numTileCols, int numTileRows) {
	// TODO these should be passed into the function
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
	writeUnsExpGolomb(&newBits, numTileCols-1);
	writeUnsExpGolomb(&newBits, numTileRows-1);
	newBits.push_back(1);
	newBits.push_back(1); // CHANGED
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, true, false);
}

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
	newBits.push_back(1); // CHANGED
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
	newBits.push_back(1); // CHANGED
	newBits.push_back(1);
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, false, true);
}

int doStitching( unsigned char* tiledBitstream,
                 int            numQualityLevels,
                 vector<vector<unsigned char*> >& bitstreams,
                 vector<vector<long> >& bitstream_Size,
				 int*           tileBitrates,
                 int            finalWidth,
                 int            finalHeight,
                 int            numTileRows,
				 int            numTileCols,
				 vector<ContextGroup>& contextGroups)
{
	int totalSize = 0;
	int tbPos = 0;
	long bitstream_Pos[numQualityLevels][contextGroups.size()];
	for (int i=0; i<contextGroups.size(); i++) {
	    for (int j=0; j<numQualityLevels; j++) {
		    bitstream_Pos[j][i] = 0;
        }
	}
	std::vector<Block> nal;

	// Get as many NALs as we have in the stream
    const int ctuSize = 32;
	const int oldCtuOffsetBitSize = ceil(log2(((finalWidth/numTileCols)/ctuSize)*((finalHeight*numTileCols)/ctuSize)));
	const int newCtuOffsetBitSize = ceil(log2((finalWidth/ctuSize)*(finalHeight/ctuSize)));
	// TODO based on num tiles and layout
	int numTiles = numTileRows * numTileCols;
	int imgCtuWidth = finalWidth / ctuSize;
	int tileCtuHeight = (finalHeight / ctuSize) / numTileRows;
	int tileCtuWidth = imgCtuWidth / numTileCols;
	int tileIdx;
	vector<int> sliceSegAddrs(numTiles); // 0 not used, but convenient for index
	for (int col=0; col<numTileCols; col++) {
		for (int row=0; row<numTileRows; row++) {
			tileIdx = (numTileRows * col) + row;
			sliceSegAddrs[tileIdx] = (row * imgCtuWidth * tileCtuHeight) + (col * tileCtuWidth);
		}
	}
	for (int i=0; i<numTiles; i++) {
		ctuOffsetBits.insert({sliceSegAddrs[i], Bitset(newCtuOffsetBitSize, sliceSegAddrs[i])});
	}
	int  i = 0;
	int  nalType;
	int  ifs_idx = -1;

	long posAfterFirstTile[numQualityLevels];
	for (int j=0; j<numQualityLevels; j++) {
        posAfterFirstTile[j] = -1;
    }

	int  iBase = 0;
	while (true)
    {
		for (int cg_idx=0; cg_idx<contextGroups.size(); cg_idx++)
		{
			// Not sure if we need to do this. This would make us go through all tiles in a context
			// group at one time, but this might mess up the value of `i`. Conversely, NOT doing this
			// will cause us to iterate all context groups at once, which does not seem right since
			// context groups may be of different sizes.
			if (cg_idx > 0) {
				iBase += contextGroups[cg_idx-1].numTileCols * numTileRows;
			}
			do
			{
                if( i >= (int)sliceSegAddrs.size() )
                {
                    // std::cerr << "line " << __LINE__ << " index i (" << i << ") is >= than number of tiles (" << numTiles << ")" << std::endl;
                }

				for (int ifs_idx=0; ifs_idx<numQualityLevels; ifs_idx++)
				{
					if (cg_idx > 0 && i == -1) {
						bitstream_Pos[ifs_idx][cg_idx] = posAfterFirstTile[ifs_idx];
						i = iBase;
					}
					nalType = getNextNAL( bitstreams[ifs_idx][cg_idx],
										&nal,
										&bitstream_Pos[ifs_idx][cg_idx],
										bitstream_Size[ifs_idx][cg_idx] );
					switch (nalType) {
						case P_SLICE:
                            // std::cerr << "line " << __LINE__ << " case P_SLICE, i is " << i << std::endl;
                            if( i >= (int)sliceSegAddrs.size() )
                            {
                                std::cerr << "line " << __LINE__ << " index i (" << i << ") is >= than number of tiles (" << numTiles << ")" << std::endl;
                                exit( -1 );
                            }

							if (tileBitrates[i] == ifs_idx) {
								modifyPSlice(&nal, (i==0), sliceSegAddrs[i], oldCtuOffsetBitSize,
											newCtuOffsetBitSize);
								std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
								totalSize += nal.size();
							}
							if (cg_idx == 0 && i == 0) {
								posAfterFirstTile[ifs_idx] = bitstream_Pos[ifs_idx][cg_idx];
							}
							if (ifs_idx == numQualityLevels-1)
                            {
                                i++;
                                // std::cerr << "line " << __LINE__ << " index i is " << i << std::endl;
                            }
							break;
						case I_SLICE:
                            // std::cerr << "line " << __LINE__ << " case I_SLICE, i is " << i << " ifs_idx=" << ifs_idx << " cg_idx=" << cg_idx << std::endl;
                            if( i >= (int)sliceSegAddrs.size() )
                            {
                                std::cerr << "line " << __LINE__ << " index i (" << i << ") is >= than number of tiles (" << numTiles << ")" << std::endl;
                                exit( -1 );
                            }

							if (tileBitrates[i] == ifs_idx) {
								modifyISlice(&nal, (i==0), sliceSegAddrs[i], oldCtuOffsetBitSize,
											newCtuOffsetBitSize);
								std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
								totalSize += nal.size();
							}
							// If this is the first tile in the first context group, save the pos
							if (cg_idx == 0 && i == 0) {
								posAfterFirstTile[ifs_idx] = bitstream_Pos[ifs_idx][cg_idx];
							}
							if (ifs_idx == numQualityLevels-1)
                            {
                                i++;
                                // std::cerr << "line " << __LINE__ << " index i is " << i << std::endl;
                            }
							break;
						case SPS:
							if (ifs_idx==0) {
								modifySPS(&nal, finalWidth, finalHeight);
								std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
								totalSize += nal.size();
							}
							break;
						case PPS:
							if (ifs_idx==0) {
								modifyPPS(&nal, numTileCols, numTileRows);
								std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
								totalSize += nal.size();
							}
							break;
						case VPS:
                            // std::cerr << "line " << __LINE__ << " case VPS, i is " << i << std::endl;
							if (ifs_idx==0) {
								std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
								totalSize += nal.size();
							}
							// I believe its now better to handle this in the case of -1 since there
							// may be multiple SEI in a context group, and we only want to stop
							// at the very last one.
							//if (ifs_idx == numQualityLevels-1) i = 0;
							break;
						case SEI:
                            // std::cerr << "line " << __LINE__ << " case SEI, i is " << i << std::endl;
							if (ifs_idx==0) {
								std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
								totalSize += nal.size();
							}
							// I believe its now better to handle this in the case of -1 since there
							// may be multiple SEI in a context group, and we only want to stop
							// at the very last one.
							//if (ifs_idx == numQualityLevels-1) i = 0;
							break;
						case OTHER:
                            // std::cerr << "line " << __LINE__ << " case OTHER, i is " << i << std::endl;
							break;
						case -1:
                            // std::cerr << "line " << __LINE__ << " case -1, i is " << i << std::endl;
							if (ifs_idx == numQualityLevels-1 && cg_idx == contextGroups.size()-1) {
								goto done;
							}
                            else if( ifs_idx == numQualityLevels-1 )
                            {
								i = -1;
                            }
					}
					nal.clear();
				}
			} while (i != -1); // -1 is simply used to indicate that this context group is done
		}
	}
 done:
	return totalSize;
}
