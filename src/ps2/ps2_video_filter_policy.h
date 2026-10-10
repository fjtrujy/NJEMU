#ifndef NJEMU_PS2_VIDEO_FILTER_POLICY_H
#define NJEMU_PS2_VIDEO_FILTER_POLICY_H

/*
 * The 704x240 PS2 raster represents vertically native 224-line arcade images.
 * Preserve hard source texels there even when non-square display pixels require
 * fractional horizontal scaling for the selected aspect-ratio presentation.
 * Other output modes retain filtering for fractional scaling.
 */
static inline int ps2_presentation_uses_nearest_filter(int output_is_240p,
    int src_width, int src_height, int dst_width, int dst_height)
{
    if (src_width <= 0 || src_height <= 0)
        return 0;
    if (output_is_240p)
        return 1;
    return dst_width >= src_width && dst_height >= src_height &&
        (dst_width % src_width) == 0 && (dst_height % src_height) == 0;
}

#endif
