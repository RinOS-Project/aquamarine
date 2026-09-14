# Aquamarine public library

Aquamarine is a surface-based drawing library.  `AqSurface` wraps ordinary
pixel memory and the public `RinRenderTarget` descriptor; it does not expose a
physical framebuffer or a compositor-private renderer object.

The library can be built directly with CMake or Meson.  Its only RinOS SDK
dependency is the public headers under `public-base/RinOS-SDK`; Unicode data is
compiled from the sibling public `libunicode` library source.
