/* yoRadio Fusion: per-device board config selector.
 *
 * Each device has its own header in boards/ and its own PlatformIO env:
 *   boards/pros3.h           [env:yoradio-um_pros3-ili9341]  (default when no board flag is set)
 *   boards/aprils_speaker.h  [env:aprils-speaker]            (-DYORADIO_BOARD_APRILS_SPEAKER=1)
 *
 * Put device settings in the board header, not here. Shared code includes this file
 * (directly or via src/core/options.h).
 */
#ifndef myoptions_h
#define myoptions_h

#if defined(YORADIO_BOARD_APRILS_SPEAKER) && YORADIO_BOARD_APRILS_SPEAKER
  #include "boards/aprils_speaker.h"
#else
  #include "boards/pros3.h"
#endif

#endif
