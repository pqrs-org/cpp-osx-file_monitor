[![Build Status](https://github.com/pqrs-org/cpp-osx-file_monitor/workflows/CI/badge.svg)](https://github.com/pqrs-org/cpp-osx-file_monitor/actions)
[![License](https://img.shields.io/badge/license-Boost%20Software%20License-blue.svg)](https://github.com/pqrs-org/cpp-osx-file_monitor/blob/main/LICENSE.md)

# cpp-osx-file_monitor

A wrapper class of File System Events API.

## Requirements

cpp-osx-file_monitor depends the following classes.

- [Nod](https://github.com/fr00b0/nod)
- [pqrs::cf::array](https://github.com/pqrs-org/cpp-cf-array)
- [pqrs::cf::string](https://github.com/pqrs-org/cpp-cf-string)
- [pqrs::dispatcher](https://github.com/pqrs-org/cpp-dispatcher)
- [pqrs::filesystem](https://github.com/pqrs-org/cpp-filesystem)
- [type_safe](https://github.com/foonathan/type_safe)

## Install

### Using package manager

You can install `include/pqrs` by using [cget](https://github.com/pfultz2/cget).

```shell
cget install pqrs-org/cget-recipes
cget install pqrs-org/cpp-osx-file_monitor
```

### Manual install

Copy `include/pqrs` directory into your include directory.
