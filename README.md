# Digital-Subband-Audio-1
------

DSA1 is a lightweight, fast, lossy audio codec using wavelets.  
It is comparable to MP2 (MPEG-1 Audio Layer II).  
It performs best at >= 160kbps (for 44.1 kHz 16-bit stereo audio).  

------

## DSA1 Features

- compression using multiresolution subband analysis instead of MDCT
   - utilizes a modified dyadic discrete wavelet transform with extra frequency resolution
- mono and stereo audio
   - joint mid/side and left/right coding are available as encoding options for stereo audio
- adaptive quantization
- two different wavelet transforms that can be switched between (depending on what the encoder decides is best)
- overlapped or non-overlapped frame support
   - overlapping reduces frame boundary artifacts at lower bitrates
- supports simple tag metadata to embed extra information about the audio
- supports the following sampling frequencies (Hz): 11025, 22050, 44100, 48000
- supports the following bit depths (per sample): unsigned 8 bits, signed 16 bits
- supports either mono (1 channel) or stereo (2 channel) audio

## Encoder Features

- single pass average bitrate (ABR) or constant quantization parameter (CQP) rate control
- basic psychoacoustic model based on subband statistics and heuristics, includes the following:
   - approximate A-weighting curve applied in combination with RMS energy of neighboring subbands ("masking")
   - oscillation of coefficients over time
   - stability/consistency of oscillation over frames
   - inter-band energy correlation
   - global energy variance
- written to be compatible with C89

------

This code follows my self-imposed restrictions:

1. Everything must be done in software, no explicit usage of hardware acceleration.
2. No floating point types or literals, everything must be integer only.
3. No 3rd party libraries, only C standard library and OS libraries for window, input, etc.
4. No languages used besides C.
5. No compiler specific features and no SIMD.
6. Single threaded.

## Compiling

### C Compiler

All you need is a C compiler.

In the root directory of the project (with all the .h and .c files):
```bash
cc -O3 -o dsa1 *.c
```

## Running Encoder

Sample usage:
```
./dsa1 e -inp=audio.wav -out=compressed.dsa -kbps=192 -wav=1 -overlap=1
```

## Running Decoder

Sample usage:
```
./dsa1 d -inp=compressed.dsa -out=decompressed.wav
```

## Tag File Usage

specify as the following command line argument to the encoder: ```-tagfile=tags.txt```  
the file `tags.txt` should contain something like this:

```
track=example track
artist=amazing artistname
comment=this is a comment that will go in the encoded file # this text after the pound sign will not be included
album=greatest hits?
genre=1 # best genre
track_num=1
total_tracks=10
year=2026
```

it supports simple # (pound sign) comments.  
run DSA1 with -v (verbose) to see the contents of the tagfile that it was able to parse in order to verify the correct information is being encoded in the DSA file.

------
NOTE: if -inp= and -out= are not specified, it will default to standard in / out (stdin/stdout).

------
If you have any questions feel free to leave a comment on YouTube OR
join the King's Crook Discord server :)

YouTube: https://www.youtube.com/@EMMIR_KC/videos

Discord: https://discord.gg/hdYctSmyQJ

itch.io: https://kingscrook.itch.io/kings-crook
