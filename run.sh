#!/usr/bin/env zsh
rm -rf build
mkdir -p build && cd build
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ln -sf $(pwd)/compile_commands.json ../
make -j4
../bin/main
