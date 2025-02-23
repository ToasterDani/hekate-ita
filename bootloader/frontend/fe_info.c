/*
 * Copyright (c) 2018 naehrwert
 * Copyright (c) 2018-2021 CTCaer
 * Copyright (c) 2018 balika011
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "fe_info.h"
#include <gfx_utils.h>
#include "../hos/hos.h"
#include "../hos/pkg1.h"
#include <libs/fatfs/ff.h>
#include <mem/heap.h>
#include <mem/smmu.h>
#include <power/bq24193.h>
#include <power/max17050.h>
#include <sec/se_t210.h>
#include <sec/tsec.h>
#include <soc/fuse.h>
#include <soc/i2c.h>
#include <soc/kfuse.h>
#include <soc/t210.h>
#include <storage/mmc.h>
#include "../storage/nx_emmc.h"
#include <storage/nx_sd.h>
#include <storage/sdmmc.h>
#include <utils/btn.h>
#include <utils/util.h>

extern void emmcsn_path_impl(char *path, char *sub_dir, char *filename, sdmmc_storage_t *storage);

#pragma GCC push_options
#pragma GCC optimize ("Os")

void print_fuseinfo()
{
	gfx_clear_partial_grey(0x1B, 0, 1256);
	gfx_con_setpos(0, 0);

	gfx_printf("\nSKU:         %X - ", FUSE(FUSE_SKU_INFO));
	switch (fuse_read_hw_state())
	{
	case FUSE_NX_HW_STATE_PROD:
		gfx_printf("Retail\n");
		break;
	case FUSE_NX_HW_STATE_DEV:
		gfx_printf("Dev\n");
		break;
	}
	gfx_printf("Sdram ID:    %d\n", fuse_read_dramid(true));
	gfx_printf("Fuses bruciati: %d / 64\n", fuse_count_burnt(fuse_read_odm(7)));
	gfx_printf("Secure key:  %08X%08X%08X%08X\n\n\n",
		byte_swap_32(FUSE(FUSE_PRIVATE_KEY0)), byte_swap_32(FUSE(FUSE_PRIVATE_KEY1)),
		byte_swap_32(FUSE(FUSE_PRIVATE_KEY2)), byte_swap_32(FUSE(FUSE_PRIVATE_KEY3)));

	gfx_printf("%kCache fuse (sbloccata):\n\n%k", 0xFF00DDFF, 0xFFCCCCCC);
	gfx_hexdump(0x7000F900, (u8 *)0x7000F900, 0x300);

	gfx_puts("\nPremi POWER per salvarli sulla scheda SD.\nPremi VOL per andare al menu'.\n");

	u32 btn = btn_wait();
	if (btn & BTN_POWER)
	{
		if (sd_mount())
		{
			char path[64];
			emmcsn_path_impl(path, "/dumps", "fuse_cached.bin", NULL);
			if (!sd_save_to_file((u8 *)0x7000F900, 0x300, path))
				gfx_puts("\nfuse_cached.bin saved!\n");

			u32 words[192];
			fuse_read_array(words);
			emmcsn_path_impl(path, "/dumps", "fuse_array_raw.bin", NULL);
			if (!sd_save_to_file((u8 *)words, sizeof(words), path))
				gfx_puts("\nfuse_array_raw.bin salvato!\n");

			sd_end();
		}

		btn_wait();
	}
}

void print_kfuseinfo()
{
	gfx_clear_partial_grey(0x1B, 0, 1256);
	gfx_con_setpos(0, 0);

	gfx_printf("%kContenuti KFuse:\n\n%k", 0xFF00DDFF, 0xFFCCCCCC);
	u32 buf[KFUSE_NUM_WORDS];
	if (!kfuse_read(buf))
		EPRINTF("CRC fail.");
	else
		gfx_hexdump(0, (u8 *)buf, KFUSE_NUM_WORDS * 4);

	gfx_puts("\nPremi POWER per salvarli sulla scheda SD.\nPremi VOL per andare al menu'.\n");

	u32 btn = btn_wait();
	if (btn & BTN_POWER)
	{
		if (sd_mount())
		{
			char path[64];
			emmcsn_path_impl(path, "/dumps", "kfuses.bin", NULL);
			if (!sd_save_to_file((u8 *)buf, KFUSE_NUM_WORDS * 4, path))
				gfx_puts("\nFatto!\n");
			sd_end();
		}

		btn_wait();
	}
}

void print_mmc_info()
{
	gfx_clear_partial_grey(0x1B, 0, 1256);
	gfx_con_setpos(0, 0);

	static const u32 SECTORS_TO_MIB_COEFF = 11;

	if (!sdmmc_storage_init_mmc(&emmc_storage, &emmc_sdmmc, SDMMC_BUS_WIDTH_8, SDHCI_TIMING_MMC_HS400))
	{
		EPRINTF("Inizializzazione eMMC fallita.");
		goto out;
	}
	else
	{
		u16 card_type;
		u32 speed = 0;

		gfx_printf("%kCID:%k\n", 0xFF00DDFF, 0xFFCCCCCC);
		switch (emmc_storage.csd.mmca_vsn)
		{
		case 2: /* MMC v2.0 - v2.2 */
		case 3: /* MMC v3.1 - v3.3 */
		case 4: /* MMC v4 */
			gfx_printf(
				" ID Vendor:  %X\n"
				" ID OEM:     %02X\n"
				" Modello:      %c%c%c%c%c%c\n"
				" Rev prd:    %X\n"
				" S/N:        %04X\n"
				" Mese/Anno: %02d/%04d\n\n",
				emmc_storage.cid.manfid, emmc_storage.cid.oemid,
				emmc_storage.cid.prod_name[0], emmc_storage.cid.prod_name[1], emmc_storage.cid.prod_name[2],
				emmc_storage.cid.prod_name[3], emmc_storage.cid.prod_name[4],	emmc_storage.cid.prod_name[5],
				emmc_storage.cid.prv, emmc_storage.cid.serial, emmc_storage.cid.month, emmc_storage.cid.year);
			break;
		default:
			break;
		}

		if (emmc_storage.csd.structure == 0)
			EPRINTF("Struttura CSD sconosciuta.");
		else
		{
			gfx_printf("%kCSD Estesa V1.%d:%k\n",
				0xFF00DDFF, emmc_storage.ext_csd.ext_struct, 0xFFCCCCCC);
			card_type = emmc_storage.ext_csd.card_type;
			char card_type_support[96];
			card_type_support[0] = 0;
			if (card_type & EXT_CSD_CARD_TYPE_HS_26)
			{
				strcat(card_type_support, "HS26");
				speed = (26 << 16) | 26;
			}
			if (card_type & EXT_CSD_CARD_TYPE_HS_52)
			{
				strcat(card_type_support, ", HS52");
				speed = (52 << 16) | 52;
			}
			if (card_type & EXT_CSD_CARD_TYPE_DDR_1_8V)
			{
				strcat(card_type_support, ", DDR52_1.8V");
				speed = (52 << 16) | 104;
			}
			if (card_type & EXT_CSD_CARD_TYPE_HS200_1_8V)
			{
				strcat(card_type_support, ", HS200_1.8V");
				speed = (200 << 16) | 200;
			}
			if (card_type & EXT_CSD_CARD_TYPE_HS400_1_8V)
			{
				strcat(card_type_support, ", HS400_1.8V");
				speed = (200 << 16) | 400;
			}

			gfx_printf(
				" Versione Spec:  %02X\n"
				" Rev Estesa:  1.%d\n"
				" ersione Dev:   %d\n"
				" Classi Cmd:   %02X\n"
				" Capacita':      %s\n"
				" Tasso max:      %d MB/s (%d MHz)\n"
				" Tasso attuale:  %d MB/s\n"
				" Supporto Tipo:  ",
				emmc_storage.csd.mmca_vsn, emmc_storage.ext_csd.rev, emmc_storage.ext_csd.dev_version, emmc_storage.csd.cmdclass,
				emmc_storage.csd.capacity == (4096 * 512) ? "High" : "Low", speed & 0xFFFF, (speed >> 16) & 0xFFFF,
				emmc_storage.csd.busspeed);
			gfx_con.fntsz = 8;
			gfx_printf("%s", card_type_support);
			gfx_con.fntsz = 16;
			gfx_printf("\n\n", card_type_support);

			u32 boot_size = emmc_storage.ext_csd.boot_mult << 17;
			u32 rpmb_size = emmc_storage.ext_csd.rpmb_mult << 17;
			gfx_printf("%kPartizioni eMMC:%k\n", 0xFF00DDFF, 0xFFCCCCCC);
			gfx_printf(" 1: %kBOOT0      %k\n    Dimensione: %5d KiB (Settori LBA: 0x%07X)\n", 0xFF96FF00, 0xFFCCCCCC,
				boot_size / 1024, boot_size / 512);
			gfx_put_small_sep();
			gfx_printf(" 2: %kBOOT1      %k\n    Dimensione: %5d KiB (Settori LBA: 0x%07X)\n", 0xFF96FF00, 0xFFCCCCCC,
				boot_size / 1024, boot_size / 512);
			gfx_put_small_sep();
			gfx_printf(" 3: %kRPMB       %k\n    Size: %5d KiB (Settori LBA: 0x%07X)\n", 0xFF96FF00, 0xFFCCCCCC,
				rpmb_size / 1024, rpmb_size / 512);
			gfx_put_small_sep();
			gfx_printf(" 0: %kGPP (USER) %k\n    Size: %5d MiB (Settori LBA: 0x%07X)\n\n", 0xFF96FF00, 0xFFCCCCCC,
				emmc_storage.sec_cnt >> SECTORS_TO_MIB_COEFF, emmc_storage.sec_cnt);
			gfx_put_small_sep();
			gfx_printf("%kTabella partizioni GPP (eMMC USER):%k\n", 0xFF00DDFF, 0xFFCCCCCC);

			sdmmc_storage_set_mmc_partition(&emmc_storage, EMMC_GPP);
			LIST_INIT(gpt);
			nx_emmc_gpt_parse(&gpt, &emmc_storage);
			int gpp_idx = 0;
			LIST_FOREACH_ENTRY(emmc_part_t, part, &gpt, link)
			{
				gfx_printf(" %02d: %k%s%k\n     Dimensione: % 5d MiB (Settori LBA 0x%07X)\n     Range LBA: %08X-%08X\n",
					gpp_idx++, 0xFFAEFD14, part->name, 0xFFCCCCCC, (part->lba_end - part->lba_start + 1) >> SECTORS_TO_MIB_COEFF,
					part->lba_end - part->lba_start + 1, part->lba_start, part->lba_end);
				gfx_put_small_sep();
			}
			nx_emmc_gpt_free(&gpt);
		}
	}

out:
	sdmmc_storage_end(&emmc_storage);

	btn_wait();
}

void print_sdcard_info()
{
	static const u32 SECTORS_TO_MIB_COEFF = 11;

	gfx_clear_partial_grey(0x1B, 0, 1256);
	gfx_con_setpos(0, 0);

	if (sd_initialize(false))
	{
		gfx_printf("%kIdentificazione Card:%k\n", 0xFF00DDFF, 0xFFCCCCCC);
		gfx_printf(
			" ID Vendor:  %02x\n"
			" ID OEM:     %c%c\n"
			" Modello:      %c%c%c%c%c\n"
			" Rev HW:     %X\n"
			" Rev FW:     %X\n"
			" S/N:        %08x\n"
			" Mese/Anno: %02d/%04d\n\n",
			sd_storage.cid.manfid, (sd_storage.cid.oemid >> 8) & 0xFF, sd_storage.cid.oemid & 0xFF,
			sd_storage.cid.prod_name[0], sd_storage.cid.prod_name[1], sd_storage.cid.prod_name[2],
			sd_storage.cid.prod_name[3], sd_storage.cid.prod_name[4],
			sd_storage.cid.hwrev, sd_storage.cid.fwrev, sd_storage.cid.serial,
			sd_storage.cid.month, sd_storage.cid.year);

		u16 *sd_errors = sd_get_error_count();
		gfx_printf("%kDati specifici scheda V%d.0:%k\n", 0xFF00DDFF, sd_storage.csd.structure + 1, 0xFFCCCCCC);
		gfx_printf(
			" Classi Cmd:    %02X\n"
			" Capacita':       %d MiB\n"
			" Larghezza bus:      %d\n"
			" Tasso attuale:   %d MB/s (%d MHz)\n"
			" Classe di velocita':    %d\n"
			" Grado UHS:      U%d\n"
			" Classe Video:    V%d\n"
			" Classe perf App: A%d\n"
			" Protezione in scrittura:  %d\n"
			" Errori SDMMC:   %d %d %d\n\n",
			sd_storage.csd.cmdclass, sd_storage.sec_cnt >> 11,
			sd_storage.ssr.bus_width, sd_storage.csd.busspeed, sd_storage.csd.busspeed * 2,
			sd_storage.ssr.speed_class, sd_storage.ssr.uhs_grade, sd_storage.ssr.video_class,
			sd_storage.ssr.app_class, sd_storage.csd.write_protect,
			sd_errors[0], sd_errors[1], sd_errors[2]); // SD_ERROR_INIT_FAIL, SD_ERROR_RW_FAIL, SD_ERROR_RW_RETRY.

		int res = f_mount(&sd_fs, "", 1);
		if (!res)
		{
			gfx_puts("Acquisendo informazioni del volume FAT...\n\n");
			f_getfree("", &sd_fs.free_clst, NULL);
			gfx_printf("%kTrovato volume %s:%k\n Liberi:    %d MiB\n Cluster: %d KiB\n",
					0xFF00DDFF, sd_fs.fs_type == FS_EXFAT ? "exFAT" : "FAT32", 0xFFCCCCCC,
					sd_fs.free_clst * sd_fs.csize >> SECTORS_TO_MIB_COEFF, (sd_fs.csize > 1) ? (sd_fs.csize >> 1) : 512);
			f_mount(NULL, "", 1);
		}
		else
		{
			EPRINTFARGS("Montaggio scheda SD fallito (Errore FatFS %d).\n"
				"Accertati che esista una partizione FAT..", res);
		}

		sdmmc_storage_end(&sd_storage);
	}
	else
	{
		EPRINTF("Inizializzazione scheda SD fallita");
		if (!sdmmc_get_sd_inserted())
			EPRINTF("Accertati che sia inserita.");
		else
			EPRINTF("Il lettore SD non è saldato correttamente.");
	}

	btn_wait();
}

void print_tsec_key()
{
	gfx_clear_partial_grey(0x1B, 0, 1256);
	gfx_con_setpos(0, 0);

	u32 retries = 0;

	tsec_ctxt_t tsec_ctxt;

	sdmmc_storage_init_mmc(&emmc_storage, &emmc_sdmmc, SDMMC_BUS_WIDTH_8, SDHCI_TIMING_MMC_HS400);

	// Read package1.
	u8 *pkg1 = (u8 *)malloc(0x40000);
	sdmmc_storage_set_mmc_partition(&emmc_storage, EMMC_BOOT0);
	sdmmc_storage_read(&emmc_storage, 0x100000 / NX_EMMC_BLOCKSIZE, 0x40000 / NX_EMMC_BLOCKSIZE, pkg1);
	sdmmc_storage_end(&emmc_storage);
	const pkg1_id_t *pkg1_id = pkg1_identify(pkg1);
	if (!pkg1_id)
	{
		EPRINTF("Unknown pkg1 version.");
		goto out_wait;
	}

	u8 keys[SE_KEY_128_SIZE * 2];
	memset(keys, 0x00, 0x20);

	tsec_ctxt.fw = (u8 *)pkg1 + pkg1_id->tsec_off;
	tsec_ctxt.pkg1 = pkg1;
	tsec_ctxt.pkg11_off = pkg1_id->pkg11_off;
	tsec_ctxt.secmon_base = pkg1_id->secmon_base;

	if (pkg1_id->kb <= KB_FIRMWARE_VERSION_600)
		tsec_ctxt.size = 0xF00;
	else if (pkg1_id->kb == KB_FIRMWARE_VERSION_620)
		tsec_ctxt.size = 0x2900;
	else if (pkg1_id->kb == KB_FIRMWARE_VERSION_700)
	{
		tsec_ctxt.size = 0x3000;
		// Exit after TSEC key generation.
		*((vu16 *)((u32)tsec_ctxt.fw + 0x2DB5)) = 0x02F8;
	}
	else
		tsec_ctxt.size = 0x3300;

	if (pkg1_id->kb == KB_FIRMWARE_VERSION_620)
	{
		u8 *tsec_paged = (u8 *)page_alloc(3);
		memcpy(tsec_paged, (void *)tsec_ctxt.fw, tsec_ctxt.size);
		tsec_ctxt.fw = tsec_paged;
	}

	int res = 0;

	while (tsec_query(keys, pkg1_id->kb, &tsec_ctxt) < 0)
	{
		memset(keys, 0x00, 0x20);

		retries++;

		if (retries > 3)
		{
			res = -1;
			break;
		}
	}

	gfx_printf("%kChiave TSEC:  %k", 0xFF00DDFF, 0xFFCCCCCC);

	if (res >= 0)
	{
		for (u32 j = 0; j < SE_KEY_128_SIZE; j++)
			gfx_printf("%02X", keys[j]);

		if (pkg1_id->kb == KB_FIRMWARE_VERSION_620)
		{
			gfx_printf("\n%kRoot TSEC: %k", 0xFF00DDFF, 0xFFCCCCCC);
			for (u32 j = 0; j < SE_KEY_128_SIZE; j++)
				gfx_printf("%02X", keys[SE_KEY_128_SIZE + j]);
		}
	}
	else
		EPRINTFARGS("ERRORE %X\n", res);

	gfx_puts("\nPremi POWER per salvarli sulla scheda SD.\nPremi VOL per andare al menu'.\n");

	u32 btn = btn_wait();
	if (btn & BTN_POWER)
	{
		if (sd_mount())
		{
			char path[64];
			emmcsn_path_impl(path, "/dumps", "tsec_keys.bin", NULL);
			if (!sd_save_to_file(keys, SE_KEY_128_SIZE * 2, path))
				gfx_puts("\nFatto!\n");
			sd_end();
		}
	}
	else
		goto out;

out_wait:
	btn_wait();

out:
	free(pkg1);
}

void print_fuel_gauge_info()
{
	int value = 0;

	gfx_printf("%kInfo stato batteria:\n%k", 0xFF00DDFF, 0xFFCCCCCC);

	max17050_get_property(MAX17050_RepSOC, &value);
	gfx_printf("Capacita' attuale:           %3d%\n", value >> 8);

	max17050_get_property(MAX17050_RepCap, &value);
	gfx_printf("Capacita' attuale:           %4d mAh\n", value);

	max17050_get_property(MAX17050_FullCAP, &value);
	gfx_printf("Capacita' (piena):          %4d mAh\n", value);

	max17050_get_property(MAX17050_DesignCap, &value);
	gfx_printf("Capacita' (design):      %4d mAh\n", value);

	max17050_get_property(MAX17050_Current, &value);
	if (value >= 0)
		gfx_printf("Corrente attuale:            %d mA\n", value / 1000);
	else
		gfx_printf("Corrente attuale:            -%d mA\n", ~value / 1000);

	max17050_get_property(MAX17050_AvgCurrent, &value);
	if (value >= 0)
		gfx_printf("Media corrente:        %d mA\n", value / 1000);
	else
		gfx_printf("Media corrente:        -%d mA\n", ~value / 1000);

	max17050_get_property(MAX17050_VCELL, &value);
	gfx_printf("Voltaggio attuale:            %4d mV\n", value);

	max17050_get_property(MAX17050_OCVInternal, &value);
	gfx_printf("Voltaggio a circuito aperto:   %4d mV\n", value);

	max17050_get_property(MAX17050_MinVolt, &value);
	gfx_printf("Min voltaggio raggiunto:    %4d mV\n", value);

	max17050_get_property(MAX17050_MaxVolt, &value);
	gfx_printf("Max voltaggio raggiunto:    %4d mV\n", value);

	max17050_get_property(MAX17050_V_empty, &value);
	gfx_printf("Voltaggio vuoto (design): %4d mV\n", value);

	max17050_get_property(MAX17050_TEMP, &value);
	if (value >= 0)
		gfx_printf("Temperatura batteria:    %d.%d oC\n", value / 10, value % 10);
	else
		gfx_printf("Temperatura batteria:    -%d.%d oC\n", ~value / 10, (~value) % 10);
}

void print_battery_charger_info()
{
	int value = 0;

	gfx_printf("%k\n\nInfo caricabatteria:\n%k", 0xFF00DDFF, 0xFFCCCCCC);

	bq24193_get_property(BQ24193_InputVoltageLimit, &value);
	gfx_printf("Limite voltaggio in input:       %4d mV\n", value);

	bq24193_get_property(BQ24193_InputCurrentLimit, &value);
	gfx_printf("Limite corrente in input:       %4d mA\n", value);

	bq24193_get_property(BQ24193_SystemMinimumVoltage, &value);
	gfx_printf("Limite voltaggio minimo:         %4d mV\n", value);

	bq24193_get_property(BQ24193_FastChargeCurrentLimit, &value);
	gfx_printf("Limite di corrente ricarica rapida: %4d mA\n", value);

	bq24193_get_property(BQ24193_ChargeVoltageLimit, &value);
	gfx_printf("Limite di corrente ricarica:      %4d mV\n", value);

	bq24193_get_property(BQ24193_ChargeStatus, &value);
	gfx_printf("Stato di carica:             ");
	switch (value)
	{
	case 0:
		gfx_printf("Non in carica\n");
		break;
	case 1:
		gfx_printf("Pre-carica\n");
		break;
	case 2:
		gfx_printf("Ricarica rapida\n");
		break;
	case 3:
		gfx_printf("Carica terminata\n");
		break;
	default:
		gfx_printf("Sconosciuto (%d)\n", value);
		break;
	}
	bq24193_get_property(BQ24193_TempStatus, &value);
	gfx_printf("Stato temperatura:        ");
	switch (value)
	{
	case 0:
		gfx_printf("Normale\n");
		break;
	case 2:
		gfx_printf("Caldo\n");
		break;
	case 3:
		gfx_printf("Fresco\n");
		break;
	case 5:
		gfx_printf("Freddo\n");
		break;
	case 6:
		gfx_printf("Bollente\n");
		break;
	default:
		gfx_printf("Sconosciuto (%d)\n", value);
		break;
	}
}

void print_battery_info()
{
	gfx_clear_partial_grey(0x1B, 0, 1256);
	gfx_con_setpos(0, 0);

	print_fuel_gauge_info();

	print_battery_charger_info();

	u8 *buf = (u8 *)malloc(0x100 * 2);

	gfx_printf("%k\n\nRegistri stato della batteria:\n%k", 0xFF00DDFF, 0xFFCCCCCC);

	for (int i = 0; i < 0x200; i += 2)
	{
		i2c_recv_buf_small(buf + i, 2, I2C_1, MAXIM17050_I2C_ADDR, i >> 1);
		usleep(2500);
	}

	gfx_hexdump(0, (u8 *)buf, 0x200);

	gfx_puts("\nPremi POWER per salvarli sulla scheda SD.\nPremi VOL per andare al menu'.\n");

	u32 btn = btn_wait();

	if (btn & BTN_POWER)
	{
		if (sd_mount())
		{
			char path[64];
			emmcsn_path_impl(path, "/dumps", "fuel_gauge.bin", NULL);
			if (sd_save_to_file((u8 *)buf, 0x200, path))
				EPRINTF("\nErrore nella creazione del filefuel.bin.");
			else
				gfx_puts("\nFatto!\n");
			sd_end();
		}

		btn_wait();
	}
	free(buf);
}

void _ipatch_process(u32 offset, u32 value)
{
	gfx_printf("%8x %8x", BOOTROM_BASE + offset, value);
	u8 lo = value & 0xff;
	switch (value >> 8)
	{
	case 0x20:
		gfx_printf("    MOVS R0, #0x%02X", lo);
		break;
	case 0xDF:
		gfx_printf("    SVC #0x%02X", lo);
		break;
	}
	gfx_puts("\n");
}

void bootrom_ipatches_info()
{
	gfx_clear_partial_grey(0x1B, 0, 1256);
	gfx_con_setpos(0, 0);

	static const u32 BOOTROM_SIZE = 0x18000;

	u32 res = fuse_read_ipatch(_ipatch_process);
	if (res != 0)
		EPRINTFARGS("Lettura ipatch fallita. Errore: %d", res);

	gfx_puts("\nPremi POWER per salvarli sulla scheda SD.\nPremi VOL per andare al menu'.\n");

	u32 btn = btn_wait();
	if (btn & BTN_POWER)
	{
		if (sd_mount())
		{
			char path[64];
			u32 iram_evp_thunks[0x200];
			u32 iram_evp_thunks_len = sizeof(iram_evp_thunks);
			res = fuse_read_evp_thunk(iram_evp_thunks, &iram_evp_thunks_len);
			if (res == 0)
			{
				emmcsn_path_impl(path, "/dumps", "evp_thunks.bin", NULL);
				if (!sd_save_to_file((u8 *)iram_evp_thunks, iram_evp_thunks_len, path))
					gfx_puts("\nevp_thunks.bin salvato!\n");
			}
			else
				EPRINTFARGS("Lettura evp_thunks fallita. Errore: %d", res);

			emmcsn_path_impl(path, "/dumps", "bootrom_patched.bin", NULL);
			if (!sd_save_to_file((u8 *)BOOTROM_BASE, BOOTROM_SIZE, path))
				gfx_puts("\nbootrom_patched.bin salvato!\n");

			u32 ipatch_backup[14];
			memcpy(ipatch_backup, (void *)IPATCH_BASE, sizeof(ipatch_backup));
			memset((void*)IPATCH_BASE, 0, sizeof(ipatch_backup));

			emmcsn_path_impl(path, "/dumps", "bootrom_unpatched.bin", NULL);
			if (!sd_save_to_file((u8 *)BOOTROM_BASE, BOOTROM_SIZE, path))
				gfx_puts("\nbootrom_unpatched.bin salvato!\n");

			memcpy((void*)IPATCH_BASE, ipatch_backup, sizeof(ipatch_backup));

			sd_end();
		}

		btn_wait();
	}
}

#pragma GCC pop_options
