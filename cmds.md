## Steps to build the docker:
Useful Website: https://www.gem5.org/documentation/general_docs/building#dependencies
0. Install the docker application on the website

1. Install the ubuntu image:

   ```cmd
   docker pull ghcr.io/gem5/ubuntu-24.04_all-dependencies:latest
   ```

2. Create a Dockerfile to set up the initial environment for your image

3. Build a new image based on the ubuntu (content in {} can be named by your preference):

   ```cmd
   docker build -t {image name} .
   ```

4.  Create the container based on your image:

   ```cmd
   docker run --name {container name} -it -v {path/of/folder including gem5}:/home/root {image name}
   ```

5. Start the container when you want use it anytime:

   ```cmd
   docker start -ai {container name}
   ```

6. Now you can see /home/root interface same as Linux with root permission. If not press Enter key to see any changes. You can compile and test the Gem5 in this container now.  

## Useful Commands 

**Build a img based on Dockerfile:**
docker build -t fyhgem5_img .

**Create container gem5_v0 based on img:**
docker run --name gem5_v0 -it -v D:\RPI\CXL_mem_ctrl\:/home/root fyhgem5_img

**Start a Container:**
docker start -ai gem5_v0

**Compile gem5:**
scons build/{ISA}/gem5.{variants} -j 12    //ISA includes x86, arm, RISCV;      variants includes opt, debug, fast

**Compile gem5 with Cache Coherence Protocol:**
scons defconfig build/X86_MSI build_opts/X86
scons setconfig build/X86_MSI RUBY_PROTOCOL_MSI=y SLICC_HTML=y
scons build/X86_MSI/gem5.opt

**Clean Compiled file:**
scons build/{ISA}/gem5.{variants} -c

**Run Scripts:**
./build/{ISA}/gem5.{variant} [gem5 options] {simulation script} [script options]

e.g build/X86/gem5.opt --debug-flags=CXLMemCtrl configs/cxl_mem/main_config.py | head -n 50

Multicore script run:
e.g build/X86_MSI/gem5.opt --debug-flags=CXLMemCtrl configs/cxl_mem/multi_core.py  