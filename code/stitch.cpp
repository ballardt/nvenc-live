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

#define CTU_SIZE 32

using namespace std;

typedef unsigned char Block;
typedef boost::dynamic_bitset<Block> Bitset;

enum NALType {
	P_SLICE = 0,
	I_SLICE,
	//VPS,
	SPS,
	PPS,
	SEI,
	OTHER,
	END_OF_STREAM,
	INITIALIZE_NALTYPE
};

std::map<int, Bitset> ctuOffsetBits;

/**
 * Given a NAL, get its type.
 * The type is dictated by the first *5* (h264) bits after the (3-byte + 1-bit + *2-bit*) border.
 * Keep it simple with bitmasks, no need to use Bitsets.
 */
NALType getNALType(std::vector<Block>& nal)
{
	NALType nalType;
	int typeBitsOffset = 3;
	// Move past any extra 0x00's. This is mostly for VPS.
    // std::cerr << __LINE__ << " nal size " << nal.size() << std::endl;
	int i = 0;
	while (nal[i+2] != 0x01) {
		typeBitsOffset++;
		i++;
	}
	unsigned char typeBits = nal[typeBitsOffset] << 3;
	typeBits >>= 3;
	printf("%02X ", typeBits);
	switch (typeBits) {
		//case 0x00:
		case 0x01:
			nalType = P_SLICE;
			break;
		//case 0x13:
		//case 0x14:
		case 0x05:
			nalType = I_SLICE;
			break;
		//case 0x20:
		//	nalType = VPS;
		//	break;
		//case 0x21:
		case 0x07:
			nalType = SPS;
			break;
		//case 0x22:
		case 0x08:
			nalType = PPS;
			break;
		//case 0x27:
		//case 0x28:
		case 0x06:
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
int getNextNAL( std::shared_ptr<ContextGroup> group, Bitrate bitrateIdx, std::vector<Block>* buf )
{
#if 0
    std::cerr << "Enter getNextNAL with " << group->getBitstreamPos( bitrateIdx ) << std::endl;
    unsigned char* p = group->getBitstream( bitrateIdx );
    for( int i=0; i<10; i++ )
    {
        for( int j=0; j<10; j++ )
        {
            std::cerr << std::setw(4) << int(p[i*10+j]) << " ";
        }
        std::cerr << std::endl;
    }
#endif
	if (group->getBitstreamPos( bitrateIdx ) >= group->getBitstreamSize( bitrateIdx )) {
		return END_OF_STREAM;
	}
	// Go past the first border and consume it.
	int zeroCounter = 0;
	unsigned char c = 0xFF;
	while ((zeroCounter < 2 || c != 0x01) && group->getBitstreamPos( bitrateIdx ) < group->getBitstreamSize( bitrateIdx )) {
		c = group->getBitstreamHere( bitrateIdx );
		buf->push_back(c);
        group->incBitstreamPos( bitrateIdx, 1 );
		if (c == 0x00) {
			zeroCounter++;
		}
	}
	// Stop when we encounter the next border. Do not consume it.
	zeroCounter = 0;
	while ((zeroCounter < 2 || c != 0x01) && group->getBitstreamPos( bitrateIdx ) < group->getBitstreamSize( bitrateIdx )) {
		c = group->getBitstreamHere( bitrateIdx );
		buf->push_back(c);
        group->incBitstreamPos( bitrateIdx, 1 );
		if (c == 0x00) {
			zeroCounter++;
		}
		else if (zeroCounter < 2 || c != 0x01) {
			zeroCounter = 0;
		}
	}
	if (zeroCounter >= 2 && (unsigned char)c == 0x01) {
        group->incBitstreamPos( bitrateIdx, -3 );
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
			printf(((*oldBits)[oldBitsPos+numZeros]) ? "1" : "0");
			newBits->push_back((*oldBits)[oldBitsPos+numZeros]);
		}
		numZeros++;
		curr = (*oldBits)[oldBitsPos+numZeros];
	}
	if (newBits != NULL) {
		for (int i=0; i<numZeros+1; i++) {
			printf(((*oldBits)[oldBitsPos+numZeros]) ? "1" : "0");
			newBits->push_back((*oldBits)[oldBitsPos+numZeros+i]);
		}
	}
	printf("\n");
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
	int mbWidthMinus1 = (width / 16) - 1;
	int mbHeightMinus1 = (height/ 16) - 1;
	printf("%d\n", mbHeightMinus1);
	// Convert the NAL to some bits
	nalToBitset(&oldBits, nal);
	// Navigate to the right spot
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 40);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 24);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 1);
	// Consume old values for width/height, insert new ones
	oldBitsPos += copyExpGolomb(&oldBits, NULL, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, NULL, oldBitsPos);
	writeUnsExpGolomb(&newBits, mbWidthMinus1);
	writeUnsExpGolomb(&newBits, mbHeightMinus1);
	// Finalize
	//doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, true, false);
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, true, true);
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
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 32);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 2);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 3);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyExpGolomb(&oldBits, &newBits, oldBitsPos);
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 1);
	// Set constrained_intra_pred_flag
	oldBitsPos += 1;
	newBits.push_back(1);
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, false, false);
}

void writeCtuOffset(Bitset* bits, unsigned int ctuOffset, int ctuOffsetBitSize) {
	for (int i=0; i<ctuOffsetBitSize; i++) {
		bits->push_back(ctuOffsetBits[ctuOffset][ctuOffsetBitSize-i-1]);
	}
}

// Set segmentAddress, slice_loop_filter_across_..., num_entrypoint_offsets
// *** MUST CHANGE LATER **** ctuOffset now corresponds to first_mb_in_slice
void modifyISlice(std::vector<Block>* nal, bool isFirstSlice, bool wasFirstSlice, int ctuOffset,
				   int oldCtuOffsetBitSize, int newCtuOffsetBitSize) {
	int oldBitsPos = 0;
	Bitset oldBits(nal->size()*8);
	Bitset newBits(0);
	// Convert the NAL to some bits
	nalToBitset(&oldBits, nal);
	printf("I slice:\n");
	for (int i=0; i<100; i++) {
		printf((oldBits[oldBitsPos+i]) ? "1" : "0");
	}
	printf("\n%d\n", ctuOffset);
	// Navigate to the right spot and make our changes
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 32);
	oldBitsPos += copyExpGolomb(&oldBits, NULL, oldBitsPos);
	writeUnsExpGolomb(&newBits, ctuOffset);
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, false, false);
}

// Set segmentAddress, slice_loop_filter_across_..., num_entrypoint_offsets
void modifyPSlice(std::vector<Block>* nal, bool isFirstSlice, bool wasFirstSlice, int ctuOffset,
				   int oldCtuOffsetBitSize, int newCtuOffsetBitSize) {
	int oldBitsPos = 0;
	Bitset oldBits(nal->size()*8);
	Bitset newBits(0);
	// Convert the NAL to some bits
	nalToBitset(&oldBits, nal);
	printf("P slice:\n");
	for (int i=0; i<100; i++) {
		printf((oldBits[i]) ? "1" : "0");
	}
	printf("\n\n");
	// Navigate to the right spot and make our changes
	oldBitsPos += copyBits(&oldBits, &newBits, oldBitsPos, 32);
	oldBitsPos += copyExpGolomb(&oldBits, NULL, oldBitsPos);
	writeUnsExpGolomb(&newBits, ctuOffset);
	// Finalize
	doneEditingNAL(nal, &newBits, &oldBits, oldBitsPos, false, false);
}

int doStitching( unsigned char* tiledBitstream,
                 int            numQualityLevels,
				 int*           tileBitrates,
                 int            finalWidth,
                 int            finalHeight,
                 int            numTileRows,
				 int            numTileCols,
				 vector<shared_ptr<ContextGroup> >& contextGroups)
{
	int totalSize = 0;
	int tbPos = 0;

    for( auto group : contextGroups )
    {
        if( group->valid() == false )
        {
            std::cerr << "Working on an invalid group struct" << std::endl;
        }
        group->clearBitstreamPos( );
    }
	std::vector<Block> nal;
	//vector<int> oldCtuOffsetBitSizes(contextGroups.size());
	//for (int i=0; i<contextGroups.size(); i++) {
	//	oldCtuOffsetBitSizes[i] = ceil(log2((contextGroups[i]->getWidth()/CTU_SIZE)
	//									   *(contextGroups[i]->getHeight()/CTU_SIZE)));
	//}
	//const int newCtuOffsetBitSize = ceil(log2((finalWidth/CTU_SIZE)*(finalHeight/CTU_SIZE)));
	int numTiles = numTileRows * numTileCols;
	//int imgCtuWidth = finalWidth / CTU_SIZE;
	//int tileCtuHeight = (finalHeight / CTU_SIZE) / numTileRows;
	//int tileCtuWidth = imgCtuWidth / numTileCols;
	int tileIdx;
	//vector<int> sliceSegAddrs(numTiles);
	//for (int col=0; col<numTileCols; col++) {
	//	for (int row=0; row<numTileRows; row++) {
	//		tileIdx = (numTileRows * col) + row;
	//		sliceSegAddrs[tileIdx] = (row * imgCtuWidth * tileCtuHeight) + (col * tileCtuWidth);
	//	}
	//}
	//for (int i=0; i<numTiles; i++) {
	//	ctuOffsetBits.insert({sliceSegAddrs[i], Bitset(newCtuOffsetBitSize, sliceSegAddrs[i])});
	//}
	tileIdx = 0;
	int  nalType;
	int  bitrateIdx = -1;
	bool printSEI = false;
	//int  baseTileIdx = 0;
	int numMbsInSlice = (finalWidth * finalHeight) / (numTiles * 16 * 16); // mb = macroblock
	int firstMb;
	for (int contextGroupIdx=0; contextGroupIdx<contextGroups.size(); contextGroupIdx++)
	{
		nalType = INITIALIZE_NALTYPE;
		//if (contextGroupIdx > 0)
		//{
		//	baseTileIdx += contextGroups[contextGroupIdx-1]->numTileCols * numTileRows;
		//}
		while (nalType != END_OF_STREAM)
		{
			for (int bitrateIdx=0; bitrateIdx<numQualityLevels; bitrateIdx++)
			{
				firstMb = (numMbsInSlice * tileIdx); //(tileIdx + numTiles * contextGroupIdx);
				nalType = getNextNAL( contextGroups[contextGroupIdx], (Bitrate)bitrateIdx, &nal );
				switch (nalType)
				{
					case P_SLICE:
						if (tileBitrates[tileIdx] == bitrateIdx)
						{
							modifyPSlice(&nal, false, false,
											firstMb,
											0,
											0);
							std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
							totalSize += nal.size();
						}
						if (bitrateIdx == numQualityLevels-1)
						{
							tileIdx++;
						}
						break;
					case I_SLICE:
						if (tileBitrates[tileIdx] == bitrateIdx)
						{
							modifyISlice(&nal, false, false,
											firstMb,
										    0,
											0);
							std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
							totalSize += nal.size();
						}
						if (bitrateIdx == numQualityLevels-1)
						{
							tileIdx++;
						}
						break;
					case SPS:
						if (bitrateIdx == 0 && contextGroupIdx == 0)
						{
							modifySPS(&nal, finalWidth, finalHeight);
							std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
							totalSize += nal.size();
						}
						break;
					case PPS:
						if (bitrateIdx == 0 && contextGroupIdx == 0)
						{
							modifyPPS(&nal, numTileCols, numTileRows);
							std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
							totalSize += nal.size();
							printSEI = true;
						}
						break;
					//case VPS:
					//	if (bitrateIdx == 0 && contextGroupIdx == 0)
					//	{
					//		std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
					//		totalSize += nal.size();
					//	}
					//	break;
					case SEI:
						if (printSEI)
						{
							std::copy(nal.begin(), nal.end(), tiledBitstream+totalSize);
							totalSize += nal.size();
							printSEI = false;
						}
						break;
					case OTHER:
					case END_OF_STREAM:
						break;
				}
				nal.clear();
			}
		}
	}
	return totalSize;
}
