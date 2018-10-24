#ifdef __cplusplus
extern "C" {
#endif

int doStitching(unsigned char* tiledBitstream, unsigned char* bitstream_0,
				unsigned char* bitstream_1, int bitstream_0Size, int bitstream_1Size,
				int* tileBitrates);
	unsigned char* bitstreams[2];
unsigned char* tiledBitstream;

#ifdef __cplusplus
}
#endif	
