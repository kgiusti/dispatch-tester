Need to patch qpid-dispatch sources to export internal stuff.  See patch file.

Need to set LD_LIBRARY_PATH to location of libqpid-dispatch.so:

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/opt/kgiusti/lib/qpid-dispatch numactl --physcpubind=7 ./ma-test
