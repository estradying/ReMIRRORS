Rewritten Mushroom Island Ring Rapid Octave Repetition Search

Requires external dependencies. Contact me on the Minecraft@Home discord server.

## Build

```bash
make
```

## Usage

```
Usage: ./ReMIRRORS [ACTION] [OPTIONS]

Actions:
  search    Search for seeds (default)
  help      Show this help message

Options:
  -t, --threads <N>       Number of worker threads (default: 12)
  -s, --start-seed <N>    Starting seed (default: random)
  -e, --end-seed <N>      End seed for a finite search range
  -h, --help              Show this help message
```

### Examples

```bash
# Search from a random seed with the default 12 threads (runs indefinitely)
./ReMIRRORS

# Use 8 worker threads
./ReMIRRORS search -t 8

# Resume or reproduce a run from a known starting seed
./ReMIRRORS search -s 12345

# Search a finite range of seeds
./ReMIRRORS search -s 0 -e 999999

# Combine options: 4 threads over a specific seed range
./ReMIRRORS search -t 4 -s 0 -e 999999
```
