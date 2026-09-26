#ifndef COMMON_CAPCOM_DRIVER_INFO_H
#define COMMON_CAPCOM_DRIVER_INFO_H

/* Canonical driver name selected by the CPS1/CPS2 ROM loader. This may differ
 * from the requested clone name when a parent driver supplies the definition. */
const char *capcom_driver_name(void);

#endif /* COMMON_CAPCOM_DRIVER_INFO_H */
