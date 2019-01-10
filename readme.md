# gmsv\_luapack\_internal

A Garry's Mod module that complements the LuaPack project.

## Compiling

The only supported compilation platform for this project on Windows is **Visual Studio 2017** on **release** mode.  
On Linux, everything should work fine as is, on **release** mode.  
For Mac OSX, any **Xcode (using the GCC compiler)** version *MIGHT* work as long as the **Mac OSX 10.7 SDK** is used, on **release** mode.  
These restrictions are not random; they exist because of ABI compatibility reasons.  
If stuff starts erroring or fails to work, be sure to check the correct line endings (\n and such) are present in the files for each OS.  

## Requirements

This project requires [garrysmod\_common][1], a framework to facilitate the creation of compilations files (Visual Studio, make, XCode, etc). Simply set the environment variable '**GARRYSMOD\_COMMON**' or the premake option '**gmcommon**' to the path of your local copy of [garrysmod\_common][1].  
We also use [SourceSDK2013][2], so set the environment variable '**SOURCE\_SDK**' or the premake option '**sourcesdk**' to the path of your local copy of [SourceSDK2013][2]. The previous links to [SourceSDK2013][2] point to my own fork of VALVe's repo and for good reason: Garry's Mod has lots of backwards incompatible changes to interfaces and it's much smaller, being perfect for automated build systems like Travis-CI.  

  [1]: https://github.com/danielga/garrysmod_common
  [2]: https://github.com/danielga/sourcesdk-minimal
