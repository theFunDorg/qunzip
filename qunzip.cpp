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
 *                   char vectors, one per line, split on \n (separators removed,
 *                   like "\n" vs chunk)
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

#include <rapidgzip/rapidgzip.hpp>

namespace
{
/* Extract a size_t from a q long (-KJ) or int (-KI) atom. Returns false on type mismatch. */
bool
asSize( K a, size_t& out )
{
    if ( a->t == -KJ ) { out = static_cast<size_t>( a->j ); return true; }
    if ( a->t == -KI ) { out = static_cast<size_t>( a->i ); return true; }
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
}  // namespace

extern "C" K
qunzip( K path, K func, K chunkSize, K nThreads )
{
    std::string filePath;
    size_t      bufferSize = 0;
    size_t      threads    = 0;

    if ( !asPath( path, filePath ) ) {
        return krr( const_cast<S>( "type: file path must be a symbol or char vector" ) );
    }
    if ( !asSize( chunkSize, bufferSize ) || bufferSize == 0 ) {
        return krr( const_cast<S>( "chunkSize: must be a non-zero long or int" ) );
    }
    if ( !asSize( nThreads, threads ) ) {
        return krr( const_cast<S>( "nThreads: must be a long or int (0 = auto)" ) );
    }

    /* krr does not copy its message, so error text must outlive the return. */
    static thread_local char errorMessage[512];

    try {
        auto fileReader = std::make_unique<rapidgzip::StandardFileReader>( filePath );
        rapidgzip::ParallelGzipReader reader( std::move( fileReader ), threads /* 0 = auto */ );

        std::string        buffer( bufferSize, '\0' );
        unsigned long long total = 0;

        /* Apply the callback to one chunk (a list of lines): func . (enlist chunk). dot()
         * does not take ownership of its arguments, so the knk() list (which does own
         * chunk) must be released here; func stays owned by the caller. ee() traps a
         * signal raised inside the callback, returning a -128 error object instead of
         * leaving the error pending. Calling back into q happens on this (the q main)
         * thread, which is required -- rapidgzip's worker threads stay internal to read().
         * Returns nullptr on success, or an error message to surface to q. */
        auto invoke = [&]( K chunk ) -> const char* {
            K args   = knk( 1, chunk );
            K result = ee( dot( func, args ) );
            r0( args );
            if ( result == nullptr ) {
                return "qunzip: callback evaluation failed";
            }
            if ( result->t == -128 ) {  /* q error raised inside the callback */
                std::snprintf( errorMessage, sizeof( errorMessage ), "%s",
                               result->s ? result->s : "error" );
                r0( result );
                return errorMessage;
            }
            r0( result );  /* discard the callback's return value */
            return nullptr;
        };

        /* Bytes read after the last newline are held back here and prepended to the next
         * read, so the callback only ever sees whole lines. */
        std::string carry;

        /* Split carry + data[0..len) on '\n' into a mixed list (0h) of char vectors, one
         * per line with the separators removed. carry (never containing a newline) is
         * prepended to the first line, so this must run before carry is reassigned. When
         * len > 0 the region always ends with '\n' (read0 semantics: that final newline
         * does not produce an empty last item); len == 0 is the final flush, where the
         * carried partial line alone forms a single-item list. */
        auto splitLines = [&]( const char* data, size_t len ) -> K {
            const size_t nLines = len == 0
                ? 1
                : static_cast<size_t>( std::count( data, data + len, '\n' ) );
            K lines = ktn( 0, static_cast<J>( nLines ) );
            const char* p   = data;
            const char* end = data + len;
            for ( size_t i = 0; i < nLines; ++i ) {
                const char* nl = p < end
                    ? static_cast<const char*>( std::memchr( p, '\n', static_cast<size_t>( end - p ) ) )
                    : end;
                const size_t bodyLen = static_cast<size_t>( nl - p );
                const size_t preLen  = i == 0 ? carry.size() : 0;
                K line = ktn( KC, static_cast<J>( preLen + bodyLen ) );
                std::memcpy( kC( line ), carry.data(), preLen );
                std::memcpy( kC( line ) + preLen, p, bodyLen );
                kK( lines )[i] = line;
                p = nl + 1;
            }
            return lines;
        };

        for ( ;; ) {
            const size_t nRead = reader.read( buffer.data(), buffer.size() );
            if ( nRead == 0 ) {
                break;  /* EOF */
            }
            total += nRead;

            /* Find the last newline in the freshly read bytes. */
            size_t lastNl = nRead;
            while ( lastNl-- > 0 && buffer[lastNl] != '\n' ) {
            }

            if ( lastNl == SIZE_MAX ) {
                /* No newline this read: keep accumulating the partial line. */
                carry.append( buffer.data(), nRead );
                continue;
            }

            /* Emit carry + complete lines (through the final newline inclusive) as a list
             * of lines; stash the trailing partial line as the new carry. */
            const size_t emitLen = lastNl + 1;
            K chunk = splitLines( buffer.data(), emitLen );

            carry.assign( buffer.data() + emitLen, nRead - emitLen );

            if ( const char* err = invoke( chunk ) ) {
                return krr( const_cast<S>( err ) );
            }
        }

        /* Flush a final line that had no trailing newline. */
        if ( !carry.empty() ) {
            K chunk = splitLines( "", 0 );
            if ( const char* err = invoke( chunk ) ) {
                return krr( const_cast<S>( err ) );
            }
        }

        return kj( static_cast<J>( total ) );
    } catch ( const std::exception& e ) {
        std::snprintf( errorMessage, sizeof( errorMessage ), "qunzip: %s", e.what() );
        return krr( errorMessage );
    } catch ( ... ) {
        return krr( const_cast<S>( "qunzip: unknown error" ) );
    }
}
