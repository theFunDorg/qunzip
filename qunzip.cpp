/* qunzip: stream-decompress a gzip file with rapidgzip and feed each
 * decompressed chunk to a q (kdb+) callback function.
 *
 * Loaded into q with 2: as a 4-argument function:
 *
 *     qunzip:`:qunzip 2:(`qunzip;4);
 *     qunzip[file; callback; chunkSizeBytes; nThreads]
 *
 *   file            symbol path (`:/data/x.gz) or char vector
 *   callback        monadic q function; receives each chunk as a mixed list (0h) of
 *                   char vectors, one per line, split on \n with the separators
 *                   removed (as read0 would)
 *   chunkSizeBytes  decompressed bytes handed to the callback per invocation (long/int)
 *   nThreads        rapidgzip parallelization; 0 = auto (use all cores) (long/int)
 *
 * Returns the total number of decompressed bytes as a long (-7h).
 * A bad path / corrupt stream / error thrown by the callback is surfaced
 * as a normal q error.
 */

#define KXVER 3
#include "k.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include <rapidgzip/rapidgzip.hpp>

namespace
{
/* krr does not copy its message, so error text must outlive the return. */
thread_local char errorMessage[512];

K
err( const char* message )
{
    return krr( const_cast<S>( message ) );
}

/* Extract a size_t from a non-negative q long (-KJ) or int (-KI) atom.
 * Returns false on type mismatch or a negative value. */
bool
asSize( K a, size_t& out )
{
    if ( a->t == -KJ && a->j >= 0 ) { out = static_cast<size_t>( a->j ); return true; }
    if ( a->t == -KI && a->i >= 0 ) { out = static_cast<size_t>( a->i ); return true; }
    return false;
}

/* Extract a file path from a symbol atom (-KS) or char vector (KC). The leading ':'
 * of a q file symbol (`:/path) is stripped. Returns false on type mismatch. */
bool
asPath( K a, std::string& out )
{
    if ( a->t == -KS ) {
        out = a->s ? a->s : "";
    } else if ( a->t == KC ) {
        out.assign( reinterpret_cast<const char*>( kC( a ) ), static_cast<size_t>( a->n ) );
    } else {
        return false;
    }
    if ( !out.empty() && out.front() == ':' ) {
        out.erase( 0, 1 );
    }
    return true;
}

/* Split carry + data on '\n' into a mixed list (0h) of char vectors, one per line,
 * separators removed. data must end with '\n'; carry never contains a newline and
 * is prepended to the first line. */
K
splitLines( std::string_view carry, std::string_view data )
{
    const J nLines = static_cast<J>( std::count( data.begin(), data.end(), '\n' ) );
    K lines = ktn( 0, nLines );
    for ( J i = 0; i < nLines; ++i ) {
        const size_t nl     = data.find( '\n' );
        const size_t preLen = i == 0 ? carry.size() : 0;
        K line = ktn( KC, static_cast<J>( preLen + nl ) );
        std::memcpy( kC( line ), carry.data(), preLen );
        std::memcpy( kC( line ) + preLen, data.data(), nl );
        kK( lines )[i] = line;
        data.remove_prefix( nl + 1 );
    }
    return lines;
}

/* Apply the callback to one chunk (a list of lines): func . enlist chunk. dot() does
 * not take ownership of its arguments, so the knk() list (which does own chunk) is
 * released here; func stays owned by the caller. ee() traps a q error raised inside
 * the callback. Must run on the q main thread. Returns nullptr on success, or an
 * error message to hand to krr. (krr itself returns nullptr, so an error cannot be
 * signalled via this function's return value directly.) */
const char*
invoke( K func, K chunk )
{
    K args   = knk( 1, chunk );
    K result = ee( dot( func, args ) );
    r0( args );
    if ( result == nullptr ) {
        return "qunzip: callback evaluation failed";
    }
    if ( result->t == -128 ) {
        std::snprintf( errorMessage, sizeof( errorMessage ), "%s",
                       result->s ? result->s : "error" );
        r0( result );
        return errorMessage;
    }
    r0( result );  /* discard the callback's return value */
    return nullptr;
}
}  // namespace

extern "C" K
qunzip( K path, K func, K chunkSize, K nThreads )
{
    std::string filePath;
    size_t      bufferSize = 0;
    size_t      threads    = 0;

    if ( !asPath( path, filePath ) ) {
        return err( "type: file path must be a symbol or char vector" );
    }
    if ( !asSize( chunkSize, bufferSize ) || bufferSize == 0 ) {
        return err( "chunkSize: must be a positive long or int" );
    }
    if ( !asSize( nThreads, threads ) ) {
        return err( "nThreads: must be a non-negative long or int (0 = auto)" );
    }

    try {
        auto fileReader = std::make_unique<rapidgzip::StandardFileReader>( filePath );
        rapidgzip::ParallelGzipReader reader( std::move( fileReader ), threads /* 0 = auto */ );

        std::string buffer( bufferSize, '\0' );
        uint64_t    total = 0;

        /* Bytes read after the last newline are held back and prepended to the next
         * chunk, so the callback only ever sees whole lines. rapidgzip's worker
         * threads stay internal to read(); the callback runs on this (the q main)
         * thread, as required. */
        std::string carry;

        for ( ;; ) {
            const size_t nRead = reader.read( buffer.data(), buffer.size() );
            if ( nRead == 0 ) {
                break;  /* EOF */
            }
            total += nRead;

            const std::string_view view( buffer.data(), nRead );
            const size_t lastNl = view.rfind( '\n' );
            if ( lastNl == view.npos ) {
                carry.append( view );  /* no newline this read: keep accumulating */
                continue;
            }

            K chunk = splitLines( carry, view.substr( 0, lastNl + 1 ) );
            carry.assign( view.substr( lastNl + 1 ) );
            if ( const char* e = invoke( func, chunk ) ) {
                return err( e );
            }
        }

        /* Flush a final line that had no trailing newline (a file ending in '\n'
         * produces no trailing empty line, matching read0). */
        if ( !carry.empty() ) {
            K line = ktn( KC, static_cast<J>( carry.size() ) );
            std::memcpy( kC( line ), carry.data(), carry.size() );
            if ( const char* e = invoke( func, knk( 1, line ) ) ) {
                return err( e );
            }
        }

        return kj( static_cast<J>( total ) );
    } catch ( const std::exception& e ) {
        std::snprintf( errorMessage, sizeof( errorMessage ), "qunzip: %s", e.what() );
        return krr( errorMessage );
    } catch ( ... ) {
        return err( "qunzip: unknown error" );
    }
}
