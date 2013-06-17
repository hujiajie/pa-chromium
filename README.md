##Parallel JavaScript (a.k.a RiverTrail) on chromium.
ParallelArray abstraction for JavaScript and Chromium ParallelArray version to enable parallel programming in JavaScript targeting multi-core CPUs and vector SSE/AVX instructions.

The goal and background of River Trail project Please refer to [upstream page](https://github.com/RiverTrail/RiverTrail/wiki)  hosted by Intel Lab. Here we try to bring RiverTrail to Chromium, which align with the same [ParallelArray API](https://github.com/RiverTrail/RiverTrail/wiki/ParallelArray) spec. What's more, we will try to extend the usage model of ParallelArray API, which may need to extend its API, to support GPUs device and GPUs preferred web computing, such as WebGL etc. 

## Code
1. chromium: https://github.com/01org/pa-chromium.git 

  use the “master” branch.
 
2. blink: https://github.com/01org/pa-blink.git 

  use the “master” branch.

3. .gclient file https://github.com/01org/pa-chromium/blob/master/.gclient 

## Build process

- fetch chromium code:

  `git clone git@github.com:01org/pa-chromium.git src`
  
  `git checkout –b master origin/master`


- fetch blink code:

  `cd src/third-party`
  
  `git clone git@github.com:01org/pa-blink.git WebKit`
  
  `git checkout –b master oirigin/master`

- gclient sync.

  Use .gclient file in the chromium repo for this project.
  To remove the unversioned directories automatically, run `gclient sync` with `-D -R`.
  To avoid generating Visual Studio projects which will be overwritten later, add the `-n` option.

- /build/gyp_chromium (optional: -Dcomponent=shared_library -Ddisable_nacl=1)

- Build.
