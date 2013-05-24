RiverTrail on chromium.

## Code
1. chromium: https://github.com/01org/chromium-rivertrail

  use the “rivertrail” branch.
 
2. RiverTrail:   https://github.com/01org/blink-rivertrail

  use the “rivertrail” branch.

3. .gclient file https://github.com/01org/chromium-rivertrail/blob/rivertrail/.gclient

## Build process

- fetch chromium code:

  `git clone git@github.com:01org/chromium-rivertrail.git src`
  
  `git checkout –b rivertrail origin/rivertrail`


- fetch RiverTrail code:

  `cd src/third-party`
  
  `git clone git@github.com:01org/blink-rivertrail.git WebKit`
  
  `git checkout –b rivertrail oirigin/rivertrail`

- gclient sync.

  To remove the unversioned directories automatically, run `gclient sync` with `-D -R`.
  To avoid generating Visual Studio projects which will be overwritten later, add the `-n` option.

- /build/gyp_chromium (optional: -Dcomponent=shared_library -Ddisable_nacl=1)

- Build.
