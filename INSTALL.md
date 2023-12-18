* Recommend OS: Ubuntu 18
* Install webcachesim library
```bash
git clone --recurse-submodules https://github.com/sunnyszy/lrb webcachesim
cd webcachesim
./scripts/install.sh
# test
./build/bin/webcachesim_cli
#output: webcachesim_cli traceFile cacheType cacheSize [--param=value]
```
If you see an error installing mongo, you may need to edit some files in the submodule per https://src.fedoraproject.org/rpms/R-testthat/blob/rawhide/f/R-testthat-sigstksz-not-constant.patch

To set up a mongo instance:
- https://www.mongodb.com/docs/manual/tutorial/install-mongodb-on-ubuntu/
- https://www.mongodb.com/basics/create-database
- https://www.mongodb.com/basics/mongodb-connection-string

* Set environment variables. Edit `~/.bashrc`:
```bash
# add binary and trace dir to path
export PATH=$PATH:${YOUR webcachesim DIR}/build/bin
export WEBCACHESIM_TRACE_DIR=${YOUR TRACE DIR}
export WEBCACHESIM_ROOT=${YOUR webcachesim DIR}
```
* [Optional] Set up a mongodb instance. LRB uses this to store tuning results.
* [Optional] pywebcachesim is a python wrapper to run multiple simulation in parallel on multiple nodes.
```shell script
cd python-package
# Install pywebcachesim package
# pip3 install -e .
SKLEARN_ALLOW_DEPRECATED_SKLEARN_PACKAGE_INSTALL=True pip3 install -e .
python3 pywebcachesim/simulate.py ${YOUR JOB CONFIG FILE} ${YOUR ALGORITHM PARAM FILE} ${YOUR TRACE PARAM FILE} ${MONGODB URI}
cd ..
```
