# ArithExec

## Note

When compile in Release mode, this project behave weird.
This may be caused by bugs in z3.
For now, please compile with debug mode by
` cmake -DCMAKE_BUILD_TYPE=Debug ..`

## Introduction
ArithExe is a integer program verifier based on symbolic execution (SE).
It well known that SE suffers path explosion due to loops and recursion.
To address this problem, a recurrence solver based on our paper [1, 2]
are integrated.

## Dependencies
This project are written in both C++ and Python.
Libraries and packages required by them are listed below.

### C++ Libraries
Version numbers indicate the version we use during the development.
Other versions may also applicable.

* Boost
* LLVM 18.1.8
* Z3 4.13.4.0
* google test (can be download automatically by provided CMakeLists.txt)
*  spdlog (also download by cmake automatically)

### Python Libraries
See `requirements.txt`


## Build
1. Make all dependencies ready.
    * Python environment can be built by installing 'requirements.txt'
    * It is required to install google test manually. It will be download by `cmake`.

2. build using `cmake`. In the project root, run the following commands:
```
mkdir build
cd build
cmake ..
make
```
it may take tens of seconds to compile the whole project.

3. Check if build successfully by running google test
This project uses google test as unit test framework.
There have been some tests included in this repo.
Run the following commands to see them.
```
cd test
ctest
```

## Usage
After building, an executable called `arith_exe` will be in `build` folder.
The following is the format to run it.
```
./arith_exe filename.c
```

## Google test
Beside used as unit tests,
google test is also used to benchmark this verifier.
In `test/benchmark`, we have included some programs that this tools can verify.
To generate test case based on them automatically,
type the following commands
```
cd test
python gen_test.py
```
This will generate `test/test_logic.cpp`, which includes test cases.
Rebuild this project and go to `build/test` to run `ctest`.

`gen_test.py` scan recursively all `.c` in benchmark folder, so feel free to add your own benchmark program in it.
