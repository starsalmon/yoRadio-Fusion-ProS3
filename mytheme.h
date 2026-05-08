//----------------------------------------------------------------------------------------------------------------
//    This file was generated on the website https://vip-cxema.org/
//    Program version: 1.2.0_03.06.2025
//    File last modified: 20:22 06.05.2026
//----------------------------------------------------------------------------------------------------------------
//    Project home       https://github.com/e2002/yoradio
//    Wiki               https://github.com/e2002/yoradio/wiki
//    Описание на 4PDA   https://4pda.to/forum/index.php?s=&showtopic=1010378&view=findpost&p=112992611
//    Как это прошить?   https://4pda.to/forum/index.php?act=findpost&pid=112992611&anchor=Spoil-112992611-2
//----------------------------------------------------------------------------------------------------------------
#ifndef _my_theme_h
#define _my_theme_h
//----------------------------------------------------------------------------------------------------------------
//    Theming of color displays
//    DSP_ST7735, DSP_ST7789, DSP_ILI9341, DSP_GC9106, DSP_ILI9225, DSP_ST7789_240
//----------------------------------------------------------------------------------------------------------------
//    *    !!! This file must be in the root directory of the sketch !!!    *
//----------------------------------------------------------------------------------------------------------------
//    Uncomment (remove double slash //) from desired line to apply color
//----------------------------------------------------------------------------------------------------------------
#define ENABLE_THEME
#ifdef  ENABLE_THEME
/*----------------------------------------------------------------------------------------------------------------*/
/*       | COLORS             |   values (0-255)  |                                                               */
/*       | color name         |    R    G    B    |                                                               */
/*----------------------------------------------------------------------------------------------------------------*/
#define COLOR_BACKGROUND        50,100,229     /*  background                                                  */
#define COLOR_STATION_NAME        255,255,255     /*  station name                                                */
#define COLOR_STATION_BG         50,164,65     /*  station name background                                     */
#define COLOR_STATION_FILL       50,164,65     /*  station name fill background                                */
#define COLOR_SNG_TITLE_1       255,255,255     /*  first title                                                 */
#define COLOR_SNG_TITLE_2         185,185,185    /*  second title                                                */
#define COLOR_WEATHER           255,255,255     /*  weather string                                              */
#define COLOR_VU_MAX            10,124,25     /*  max of VU meter                                             */
#define COLOR_VU_MIN            50,164,65     /*  min of VU meter                                             */
#define COLOR_CLOCK              254,217,34     /*  clock color                                                 */
#define COLOR_CLOCK_BG           0,0,0     /*  clock color background                                      */
#define COLOR_SECONDS             254,217,34     /*  seconds color (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)        */
#define COLOR_DAY_OF_W          254,217,34     /*  day of week color (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)    */
#define COLOR_DATE                255,255,255     /*  date color (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)           */
#define COLOR_HEAP              255,168,162     /*  heap string                                                 */
#define COLOR_BUFFER            50,164,65     /*  buffer line                                                 */
#define COLOR_IP                 254,217,34    /*  ip address                                                  */
#define COLOR_VOLUME_VALUE      254,217,34     /*  volume string (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)        */
#define COLOR_RSSI              254,217,34     /*  rssi                                                        */
#define COLOR_VOLBAR_OUT        255,255,255     /*  volume bar outline                                          */
#define COLOR_VOLBAR_IN         254,217,34     /*  volume bar fill                                             */
#define COLOR_DIGITS            255,255,255     /*  volume / station number                                     */
#define COLOR_DIVIDER             255,255,255     /*  divider color (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)        */
#define COLOR_BITRATE           254,217,34     /*  bitrate                                                     */
#define COLOR_PL_CURRENT          255,255,255     /*  playlist current item                                       */
#define COLOR_PL_CURRENT_BG      50,164,65     /*  playlist current item background                            */
#define COLOR_PL_CURRENT_FILL    50,164,65     /*  playlist current item fill background                       */
#define COLOR_PLAYLIST_0        254,217,34     /*  playlist string 0                                           */
#define COLOR_PLAYLIST_1        214,177,0     /*  playlist string 1                                           */
#define COLOR_PLAYLIST_2        174,137,0     /*  playlist string 2                                           */
#define COLOR_PLAYLIST_3          144,107,0     /*  playlist string 3                                           */
#define COLOR_PLAYLIST_4          104,67,0     /*  playlist string 4                                           */


#endif  /* #ifdef  ENABLE_THEME */
#endif  /* #define _my_theme_h  */
