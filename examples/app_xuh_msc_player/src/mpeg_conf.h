#ifndef MPEG_CONF_H_
#define MPEG_CONF_H_

/* The player decodes MP2 to saturating S32 PCM, using the XS3 synthesis
 * window. Set MPEG_AUDIO_FORMAT_FLOAT for lib_mpeg's default float PCM. */
#ifndef MPEG_AUDIO_FORMAT
#define MPEG_AUDIO_FORMAT MPEG_AUDIO_FORMAT_S32
#endif

#endif
