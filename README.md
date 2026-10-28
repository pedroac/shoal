# Shoal

A visual scene graph and scene flow editor for indie developers who build their own
game engines. Shoal is part of the Sprat ecosystem: it places sprites packed with
[sprat-cli](https://github.com/pedroac/sprat-cli), along with marks, paths, regions,
cameras and UI elements, and declares how scenes follow one another.

Shoal is declarative, not a game engine. It exports plain data (JSON, and a raw
format made to be parsed in one pass) that any engine in C, C++, Rust or JS can load,
so positions and scene transitions stop being hardcoded.

**Status:** early development. The design is in [docs/DESIGN.md](docs/DESIGN.md).

## Building

Shoal needs CMake 3.24 or later, a C++20 compiler and Qt 6.5 or later.
[sprat-lib](https://github.com/pedroac/sprat-lib) is downloaded during configuration.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

| Option | Default | Effect |
|---|---|---|
| `BUILD_TESTING` | `ON` | Build the tests |
| `SHOAL_SANITIZE` | `OFF` | Build with AddressSanitizer and UBSan (GCC/Clang) |
| `FETCHCONTENT_SOURCE_DIR_SPRAT` | | Use a local sprat-lib checkout, e.g. `../sprat-lib` |

## License

MIT. See [LICENSE](LICENSE).
