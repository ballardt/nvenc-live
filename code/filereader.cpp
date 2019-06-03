#include <string.h>
#include <iostream>

#include "filereader.h"

FileReader::FileReader( char* filename, size_t width, size_t height, size_t padded_height, int num_splits )
    : _filename( filename )
    , _file(0)
    , _orig_width(width)
    , _orig_height(height)
    , _padded_height( padded_height )
    , _num_splits( num_splits )
{
    _file = fopen( _filename, "rb" );
    if( _file )
    {
        _stacked_width  = _orig_width    / _num_splits;
        _stacked_height = _padded_height * _num_splits;
        _orig_cache = new Planeset( _orig_width, _orig_height );

        for( int i=0; i<10;i++ )
        {
            _stacked_cache.push_back( new Planeset( _stacked_width, _stacked_height ) );
        }

        _thread = new std::thread( &FileReader::run, this );
    }
    else
    {
        printf("Error: could not open input file\n");
    }
}

FileReader::~FileReader( )
{
    _thread->join();
    delete _thread; 

    delete _orig_cache;

    for( auto ptr : _stacked_cache ) delete ptr;
    _stacked_cache.clear();
}

bool FileReader::ok() const 
{
    return ( _file != 0 );
}

/**
 * Get the next frame, consisting of a Y, U, and V component.
 * Returns a frame pointer if a frame was available, or 0 if there are no frames
 * left in the file
 */
Planeset* FileReader::getNextFrame( )
{
    std::unique_lock<std::mutex> lock( _stack_lock );

    while( _ready.empty() )
    {
        _stack_cond.wait( lock );
    }
    Planeset* ptr = _ready.front();
    _ready.pop_front();
    lock.unlock();
    std::cerr << ".";
    return ptr;
}

void FileReader::run( )
{
    size_t ySize = _orig_width * _orig_height;

    Planeset* ptr = 0;
    while( ptr = readNextFrame( ySize ) )
    {
        std::lock_guard<std::mutex> lock( _stack_lock );
        _ready.push_back( ptr );
        _stack_cond.notify_all();
    }

    _ready.push_back( 0 );
    _stack_cond.notify_all();
}

Planeset* FileReader::readNextFrame( int ySize )
{
	Planeset* ptr = _orig_cache;

	int uvSize = ySize / 4;
	// int yRes, uRes, vRes;
	if (fread(ptr->y, sizeof(unsigned char), ySize,  _file) != ySize ||
		fread(ptr->u, sizeof(unsigned char), uvSize, _file) != uvSize ||
		fread(ptr->v, sizeof(unsigned char), uvSize, _file) != uvSize)
    {
        if( feof(_file) )
        {
            fclose( _file );
        }
        else
        {
		    perror("Problem in readNextFrame");
        }
		return 0;
	}

    return rearrangeFrame( ptr );
}

void FileReader::yieldFrame( Planeset* frame )
{
    std::lock_guard<std::mutex> lock( _stack_lock );
    _stacked_cache.push_back( frame );
    _stack_cond.notify_all( );
}

Planeset* FileReader::rearrangeFrame( Planeset* input )
{
    Planeset* rearranged = 0;

    {
        std::unique_lock<std::mutex> lock( _stack_lock );
        while( _stacked_cache.empty() )
        {
            _stack_cond.wait( lock );
        }
        rearranged = _stacked_cache.front();
        _stacked_cache.pop_front(); 
        lock.unlock();
    }

    const int yWidth  = _orig_width;
	const int uvWidth = yWidth / 2;
    const int yHeight  = _padded_height;
	const int uvHeight = yHeight / 2;
	rearrangeFrameComponent( input->y, rearranged->y, yWidth, yHeight, _num_splits);
	rearrangeFrameComponent( input->u, rearranged->u, uvWidth, uvHeight, _num_splits);
	rearrangeFrameComponent( input->v, rearranged->v, uvWidth, uvHeight, _num_splits);

    return rearranged;
}

void FileReader::rearrangeFrameComponent( unsigned char* inComponent,
                                          unsigned char* outComponent,
                                          int origWidth, int origHeight,
                                          int numSplits)
{
	int newWidth = origWidth / numSplits;
	for (int i=0; i<numSplits; i++) {
		for (int j=0; j<origHeight; j++) {
			int oldIdx = (i*newWidth) + (j*origWidth);
			int newIdx = (j*newWidth) + (i*origHeight*newWidth);
			memcpy( outComponent+newIdx, inComponent+oldIdx, newWidth ); 
		}
	}
}

