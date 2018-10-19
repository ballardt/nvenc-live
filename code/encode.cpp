#include <fstream>
#include <iterator>
#include <iostream>
#include <vector>
#include <boost/dynamic_bitset.hpp>

typedef unsigned char Block;
typedef boost::dynamic_bitset<Block> Bitset;

int getNextNAL(std::ifstream& ifs, std::vector<Block>* buf) {
	// Go past the first border and consume it.
	int zeroCounter = 0;
	char c = 0xFF;
	int bufIdx = 0;
	while ((zeroCounter < 2 || (unsigned char)c != 0x01)
		   && ifs.peek() != std::ifstream::traits_type::eof()) {
		ifs.read(&c, 1);
		//buf[bufIdx] = (unsigned char)c;
		buf->push_back((unsigned char)c);
		bufIdx++;
		if ((unsigned char)c == 0x00) {
			zeroCounter++;
		}
	}
	if (ifs.peek() == std::ifstream::traits_type::eof()) {
		return -1;
	}
	// Stop when we encounter the next border. Do not consume it.
	zeroCounter = 0;
	while ((zeroCounter < 2 || (unsigned char)c != 0x01)
		   && ifs.peek() != std::ifstream::traits_type::eof()) {
		ifs.read(&c, 1);
		//buf[bufIdx] = (unsigned char)c;
		buf->push_back((unsigned char)c);
		bufIdx++;
		if ((unsigned char)c == 0x00) {
			zeroCounter++;
		}
		else if (zeroCounter < 2 || (unsigned char)c != 0x01) {
			zeroCounter = 0;
		}
	}
	if (ifs.peek() == std::ifstream::traits_type::eof()) {
		return -1;
	}
	ifs.putback((unsigned char) 0x01);
	ifs.putback((unsigned char) 0x00);
	ifs.putback((unsigned char) 0x00);
	buf->pop_back();
	buf->pop_back();
	buf->pop_back();
	return bufIdx-3;
}

void copyBits(Bitset* oldBits, Bitset* newBits, int oldBitsPos, int newBitsPos, int numBits) {
	for (int i=0; i<numBits; i++) {
		//(*newBits)[newBitsPos+i] = (*oldBits)[oldBitsPos+i];
		newBits->push_back((*oldBits)[oldBitsPos+i]);
	}
}

int modifySPS(std::vector<Block>* nal, int nalLen) {
	// TODO calculate new size? Can use push_back if necessary, but this is sloppy
	int oldBitsPos = 0;
	int newBitsPos = 0;
	Bitset oldBits(nalLen*8);
	Bitset newBits(0);

	// Load the SPS into a bitset
	boost::from_block_range(nal->begin(), nal->end(), oldBits);

	// Copy over the old SPS into the new bitset, 
	copyBits(&oldBits, &newBits, oldBitsPos, newBitsPos, oldBits.size());

	// Place the new bits into the NAL
	nal->clear();
	boost::to_block_range(newBits, std::back_inserter(*nal));
	return nal->size();
}

int main(int, char*[]) {
	std::ifstream ifs("../../videos/stitched.hevc", std::ios::binary);
	std::ofstream ofs("test.hevc", std::ios::binary);
	std::vector<Block> rawNAL;

	int vpsLen = getNextNAL(ifs, &rawNAL);
	ofs.write((char*)&rawNAL[0], rawNAL.size());
	rawNAL.clear();

	int spsLen = getNextNAL(ifs, &rawNAL);
	modifySPS(&rawNAL, spsLen);
	ofs.write((char*)&rawNAL[0], rawNAL.size());
	rawNAL.clear();

	int ppsLen = getNextNAL(ifs, &rawNAL);
	ofs.write((char*)&rawNAL[0], ppsLen);
	rawNAL.clear();

	int nalLen = 0;
	while (nalLen != -1) {
		nalLen = getNextNAL(ifs, &rawNAL);
		ofs.write((char*)&rawNAL[0], nalLen);
		rawNAL.clear();
	}
}
