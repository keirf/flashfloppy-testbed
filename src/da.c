/*
 * da.c
 * 
 * Direct Access protocol to FlashFloppy.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define DA_SD_FM_CYL  254
#define DA_DD_MFM_CYL 255

#define CMD_NOP          0
#define CMD_SET_LBA      1 /* p[0-3] = LBA (little endian) */
#define CMD_SET_CYL      2 /* p[0] = drive A cyl, p[1] = drive B cyl */
#define CMD_SET_RPM      3 /* p[0] = 0x00 -> default, 0xFF -> 300 RPM */
#define CMD_SELECT_IMAGE 4 /* p[0-1] = slot # (little endian) */
#define CMD_SELECT_NAME 10 /* p[] = name (c string) */

const static char sig[] = "HxCFEDA";
const static struct idam idam = { 255, 0, 0, 2 };

static void da_check_status(void *p)
{
    struct da_status_sector *dass = (struct da_status_sector *)p;
    ibm_mfm_read_sector(p, &idam);
    WARN_ON(strcmp(dass->sig, sig));
}

void da_select_image(const char *name)
{
    uint8_t *p = alloca(512);
    struct da_cmd_sector *dacs = (struct da_cmd_sector *)p;

    floppy_seek(DA_DD_MFM_CYL, 0);
    cur_drive->ticks_per_cell = sysclk_us(2);
    da_check_status(p);

    strcpy(dacs->sig, sig);
    dacs->cmd = CMD_SELECT_NAME;
    strcpy((char *)dacs->param, name);
    ibm_mfm_write_sector(p, &idam, 4);

    da_check_status(p);
    floppy_disk_change();
}

void da_test(void)
{
    uint8_t *p = alloca(512);
    struct da_status_sector *dass = (struct da_status_sector *)p;
    const static struct idam fm_idam = { 254, 0, 0, 2 };

    floppy_select(0);

    /* Check the FM interface. */
    floppy_seek(DA_SD_FM_CYL, 0);
    cur_drive->ticks_per_cell = sysclk_us(4);
    ibm_fm_read_sector(p, &fm_idam);
    WARN_ON(strcmp(dass->sig, sig));

    /* Check the MFM interface. */
    floppy_seek(DA_DD_MFM_CYL, 0);
    cur_drive->ticks_per_cell = sysclk_us(2);
    ibm_mfm_read_sector(p, &idam);
    WARN_ON(strcmp(dass->sig, sig));
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
