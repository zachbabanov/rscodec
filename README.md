# RS
## Reed-Solomon Correction Codes in C

RS is a fast library for Erasure Correction Coding.
From a block of equally sized original data pieces, it generates recovery
symbols that can be used to recover lost original data.


#### Motivation:

It gets slower as O(N Log N) in the input data size, and its inner loops are
vectorized using the best approaches available on modern processors, using the
fastest finite fields (8-bit or 16-bit Galois fields with Cantor basis {2}).

It sets new speed records for MDS encoding and decoding of large data,
achieving over 1.2 GB/s to encode with the AVX2 instruction set on a single core.

Example applications are data recovery software and data center replication.


#### Encoder API:

Preconditions:

* The original and recovery data must not exceed 65536 pieces.
* The recovery_count <= original_count.
* The buffer_bytes must be a multiple of 64.
* Each buffer should have the same number of bytes.
* Even the last piece must be rounded up to the block size.

```
#include "codec.h"
```

For full documentation please read `codec.h`.

+ `codec_init()` : Initialize library.
+ `codec_encode_work_count()` : Calculate the number of work_data buffers to provide to encode().
+ `encode()`: Generate recovery data.


#### Decoder API:

For full documentation please read `codec.h`.

+ `codec_init()` : Initialize library.
+ `codec_decode_work_count()` : Calculate the number of work_data buffers to provide to decode().
+ `decode()` : Recover original data.


#### Benchmarks:

On the the 22 nm "Haswell/Crystalwell" 2.8 GHz Intel Core i7-4980HQ processor, compiled with Visual Studio 2017 RC:

~~~
Encoder(8.192 MB in 128 pieces, 128 losses): Input=2102.67 MB/s, Output=2102.67 MB/s
Decoder(8.192 MB in 128 pieces, 128 losses): Input=686.212 MB/s, Output=686.212 MB/s

Encoder(64 MB in 1000 pieces, 200 losses): Input=2194.94 MB/s, Output=438.988 MB/s
Decoder(64 MB in 1000 pieces, 200 losses): Input=455.633 MB/s, Output=91.1265 MB/s

Encoder(2097.15 MB in 32768 pieces, 32768 losses): Input=451.168 MB/s, Output=451.168 MB/s
Decoder(2097.15 MB in 32768 pieces, 32768 losses): Input=190.471 MB/s, Output=190.471 MB/s
~~~

2 GB of 64 KB pieces encoded in 4.6 seconds, and worst-case recovery in 11 seconds.


#### FFT Data Layout:

We pack the data into memory in this order:

~~~
[Recovery Data (Power of Two = M)] [Original Data] [Zero Padding out to 65536]
~~~

For encoding, the placement is implied instead of actual memory layout.
For decoding, the layout is explicitly used.


#### Encoder algorithm:

The encoder is described in {3}.  Operations are done O(K Log M),
where K is the original data size, and M is up to twice the
size of the recovery set.

Roughly in brief:

~~~
Recovery = FFT( IFFT(Data_0) xor IFFT(Data_1) xor ... )
~~~

It walks the original data M chunks at a time performing the IFFT.
Each IFFT intermediate result is XORed together into the first M chunks of
the data layout.  Finally the FFT is performed.


Encoder optimizations:
* The first IFFT can be performed directly in the first M chunks.
* The zero padding can be skipped while performing the final IFFT.
Unrolling is used in the code to accomplish both these optimizations.
* The final FFT can be truncated also if recovery set is not a power of 2.
It is easy to truncate the FFT by ending the inner loop early.
* The decimation-in-time (DIT) FFT is employed to calculate two layers at a time, rather than writing each layer out and reading it back in for the next layer of the FFT.


#### Decoder algorithm:

The decoder is described in {1}.  Operations are done O(N Log N), where N is up
to twice the size of the original data as described below.

Roughly in brief:

~~~
Original = -ErrLocator * FFT( Derivative( IFFT( ErrLocator * ReceivedData ) ) )
~~~


#### Precalculations:

At startup initialization, FFTInitialize() precalculates FWT(L) as
described by equation (92) in {1}, where L = Log[i] for i = 0..Order,
Order = 256 or 65536 for FF8/16.  This is stored in the LogWalsh vector.

It also precalculates the FFT skew factors (s_i) as described by
equation (28).  This is stored in the FFTSkew vector.

For memory workspace N data chunks are needed, where N is a power of two
at or above M + K.  K is the original data size and M is the next power
of two above the recovery data size.  For example for K = 200 pieces of
data and 10% redundancy, there are 20 redundant pieces, which rounds up
to 32 = M.  M + K = 232 pieces, so N rounds up to 256.


#### Online calculations:

At runtime, the error locator polynomial is evaluated using the
Fast Walsh-Hadamard transform as described in {1} equation (92).

At runtime the data is explicit laid out in workspace memory like this:
~~~
[Recovery Data (Power of Two = M)] [Original Data (K)] [Zero Padding out to N]
~~~

Data that was lost is replaced with zeroes.
Data that was received, including recovery data, is multiplied by the error
locator polynomial as it is copied into the workspace.

The IFFT is applied to the entire workspace of N chunks.
Since the IFFT starts with pairs of inputs and doubles in width at each
iteration, the IFFT is optimized by skipping zero padding at the end until
it starts mixing with non-zero data.

The formal derivative is applied to the entire workspace of N chunks.

The FFT is applied to the entire workspace of N chunks.
The FFT is optimized by only performing intermediate calculations required
to recover lost data.  Since it starts wide and ends up working on adjacent
pairs, at some point the intermediate results are not needed for data that
will not be read by the application.  This optimization is implemented by
the ErrorBitfield class.

Finally, only recovered data is multiplied by the negative of the
error locator polynomial as it is copied into the front of the
workspace for the application to retrieve.


#### Finite field arithmetic optimizations:

For faster finite field multiplication, large tables are precomputed and
applied during encoding/decoding on 64 bytes of data at a time using
SSSE3 or AVX2 vector instructions and the ALTMAP approach from Jerasure.

Addition in this finite field is XOR, and a vectorized memory XOR routine
is also used.
