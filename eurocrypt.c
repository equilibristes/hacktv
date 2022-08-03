/* hacktv - Analogue video transmitter for the HackRF                    */
/*=======================================================================*/
/* Copyright 2020 Alex L. James                                          */
/* Copyright 2020 Philip Heron <phil@sanslogic.co.uk>                    */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "video.h"
#include <time.h>

#define ECM  0
#define HASH 1

#define EC_M    0x20
#define EC_S    0x30
#define EC_3DES 0x31

#define ROTATE_LEFT  1
#define ROTATE_RIGHT 2

/* Data for EC controlled-access decoding */
const static ec_mode_t _ec_modes[] = {
	{ "tvs",          EC_S,    EC_S, { 0x5C, 0x8B, 0x11, 0x2F, 0x99, 0xA8, 0x2C }, { 0x00, 0x2B, 0x50 }, { "06/02/1999" }, { 0xFF, 0x00 }, "TV-S (S2)" },
	{ "ctvs",  	      EC_S,    EC_S, { 0x27, 0x82, 0xC5, 0xA3, 0x2D, 0x34, 0xD4 }, { 0x00, 0x2B, 0x20 }, { "06/02/1999" }, { 0xFF, 0x00 }, "CTV (S2)" },
	{ "rdv",  	      EC_S,    EC_S, { 0x38, 0xAB, 0x86, 0xD4, 0xFF, 0x1E, 0xBE }, { 0x00, 0x2D, 0x10 }, { "06/08/1996" }, { 0xFF, 0x00 }, "RDV (S2)" },
	{ "eros",  	      EC_S,    EC_S, { 0x3E, 0xC5, 0x54, 0x92, 0x61, 0x7D, 0x24 }, { 0x00, 0x2E, 0x10 }, { "01/01/2000" }, { 0xFF, 0x00 }, "Eros (S2)" },
	{ "ctv",          EC_M,    EC_M, { 0x84, 0x66, 0x30, 0xE4, 0xDA, 0xFA, 0x23 }, { 0x00, 0x04, 0x38 }, { "01/11/1995" }, { 0xFF, 0x00 }, "CTV (M)" },
	{ "tvplus",       EC_M,    EC_M, { 0x12, 0x06, 0x28, 0x3A, 0x4B, 0x1D, 0xE2 }, { 0x00, 0x2C, 0x08 }, { "01/11/1995" }, { 0xFF, 0x00 }, "TVPlus (M)" },
	{ "tv1000",       EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "01/11/1995" }, { 0xFF, 0x00 }, "TV 1000 (M)" },
	{ "tv3",          EC_M,    EC_M, { 0xE9, 0xF3, 0x34, 0x36, 0xB0, 0xBB, 0xF8 }, { 0x00, 0x04, 0x0C }, { "01/11/1996" }, { 0xFF, 0x00 }, "TV3 (M)" },
	{ "filmnet",      EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "TODAY"      }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "nrk",          EC_S,    EC_M, { 0xE7, 0x19, 0x5B, 0x7C, 0x47, 0xF4, 0x66 }, { 0x47, 0x52, 0x00 }, { "06/02/1999" }, { 0xFF, 0x00 }, "NRK (S2)" },
	{ "tv2",          EC_S,    EC_M, { 0x70, 0xBF, 0x6E, 0x51, 0x9F, 0xB8, 0xA6 }, { 0x47, 0x51, 0x00 }, { "06/02/1999" }, { 0xFF, 0x00 }, "TV2 Norway (S2)" },
	{ "visiopass",    EC_M,    EC_M, { 0x68, 0x67, 0x24, 0x50, 0xF1, 0x98, 0x72 }, { 0x00, 0x24, 0x08 }, { "TODAY"      }, { 0xFF, 0x00 }, "Visiopass (M)" },
	{ "tvcable",      EC_M,    EC_M, { 0xDA, 0xCF, 0xEB, 0x94, 0x44, 0x55, 0x56 }, { 0x00, 0x0C, 0x09 }, { "TODAY"      }, { 0xFF, 0x00 }, "TV Cable (M)" },
	{ "cplusfr",      EC_M,    EC_M, { 0xEC, 0xA6, 0xE8, 0x4E, 0x10, 0x41, 0x6F }, { 0x10, 0x00, 0x18 }, { "TODAY"      }, { 0xFF, 0x00 }, "Canal+ 4/3 (M)" },
	{ "cplusfr169",   EC_M,    EC_M, { 0x34, 0x94, 0x2B, 0x9B, 0xE5, 0xC1, 0xA2 }, { 0x10, 0x00, 0x28 }, { "TODAY"      }, { 0xFF, 0x00 }, "Canal+ 16/9 (M)" },
	{ "cplus",     EC_3DES, EC_3DES, { 0x62, 0xA7, 0x01, 0xA0, 0x5E, 0x8B, 0xB9,   /* Key 02 and key 03 - index key E */
	                                   0xCB, 0x86, 0x67, 0x27, 0x5C, 0x53, 0x17 }, { 0x00, 0x2B, 0x1E }, { "06/02/1999" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
        { "tv1000MM000418",     EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "01/11/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "filmnetMM002808",    EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "28/02/1993" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "nrk2866475200",      EC_S,    EC_M, { 0xE7, 0x19, 0x5B, 0x7C, 0x47, 0xF4, 0x66 }, { 0x47, 0x52, 0x00 }, { "25/06/1997" }, { 0xFF, 0x02 }, "NRK (S2)" },
	{ "tv22866475100",      EC_S,    EC_M, { 0x70, 0xBF, 0x6E, 0x51, 0x9F, 0xB8, 0xA6 }, { 0x47, 0x51, 0x00 }, { "25/06/1997" }, { 0xFF, 0xC0 }, "TV2 Norway (S2)" },
	{ "nrk4588475200",      EC_S,    EC_M, { 0xE7, 0x19, 0x5B, 0x7C, 0x47, 0xF4, 0x66 }, { 0x47, 0x52, 0x00 }, { "25/06/1997" }, { 0xFF, 0x02 }, "NRK (S2)" },
	{ "tv24588475100",      EC_S,    EC_M, { 0x70, 0xBF, 0x6E, 0x51, 0x9F, 0xB8, 0xA6 }, { 0x47, 0x51, 0x00 }, { "25/06/1997" }, { 0xFF, 0xC0 }, "TV2 Norway (S2)" },
	{ "nrk7351475200",      EC_S,    EC_M, { 0xE7, 0x19, 0x5B, 0x7C, 0x47, 0xF4, 0x66 }, { 0x47, 0x52, 0x00 }, { "25/06/1997" }, { 0xFF, 0x02 }, "NRK (S2)" },
	{ "tv27351475100",      EC_S,    EC_M, { 0x70, 0xBF, 0x6E, 0x51, 0x9F, 0xB8, 0xA6 }, { 0x47, 0x51, 0x00 }, { "25/06/1997" }, { 0xFF, 0xC0 }, "TV2 Norway (S2)" },
	{ "tv25581475100",      EC_S,    EC_M, { 0x70, 0xBF, 0x6E, 0x51, 0x9F, 0xB8, 0xA6 }, { 0x47, 0x51, 0x00 }, { "05/09/1992" }, { 0xFF, 0xC0 }, "TV2 Norway (S2)" },
	{ "tv25270475200",      EC_S,    EC_M, { 0xE7, 0x19, 0x5B, 0x7C, 0x47, 0xF4, 0x66 }, { 0x47, 0x52, 0x00 }, { "01/04/1996" }, { 0xFF, 0x02 }, "NRK (S2)" },
	{ "tv25270475100",      EC_S,    EC_M, { 0x70, 0xBF, 0x6E, 0x51, 0x9F, 0xB8, 0xA6 }, { 0x47, 0x51, 0x00 }, { "05/09/1992" }, { 0xFF, 0xC0 }, "TV2 Norway (S2)" },
	{ "ctv9683000438",      EC_S,    EC_S, { 0X19, 0XB2, 0X40, 0XB3, 0X6E, 0XB3, 0XD7 }, { 0x00, 0x2B, 0x20 }, { "01/04/1996" }, { 0xFF, 0x00 }, "CTV (S2)" },
	{ "ctv2886000438",      EC_S,    EC_S, { 0X19, 0XB2, 0X40, 0XB3, 0X6E, 0XB3, 0XD7 }, { 0x00, 0x2B, 0x20 }, { "01/04/1996" }, { 0xFF, 0x00 }, "CTV (S2)" },
	{ "ctv9594000438",      EC_S,    EC_S, { 0X19, 0XB2, 0X40, 0XB3, 0X6E, 0XB3, 0XD7 }, { 0x00, 0x2B, 0x20 }, { "15/12/1997" }, { 0xFF, 0x7F }, "CTV (S2)" },
	{ "rdv5718",            EC_S,    EC_S, { 0xFE, 0x6D, 0x9A, 0xBB, 0xEB, 0x97, 0xFB }, { 0x00, 0x2D, 0x91 }, { "01/12/1998" }, { 0xFF, 0x00 }, "RDV (S2)" },
        { "rdv4717",            EC_S,    EC_S, { 0xFE, 0x6D, 0x9A, 0xBB, 0xEB, 0x97, 0xFB }, { 0x00, 0x2D, 0x13 }, { "01/01/2000" }, { 0xFF, 0x00 }, "RDV (S2)" },
	{ "tvplus6185002C08",   EC_M,    EC_M, { 0x12, 0x06, 0x28, 0x3A, 0x4B, 0x1D, 0xE2 }, { 0x00, 0x2C, 0x08 }, { "01/12/1996" }, { 0xFF, 0x00 }, "TVPlus (M)" },
	{ "viasat8309000408",   EC_M,    EC_M, { 0x73, 0x13, 0xC6, 0x8A, 0x35, 0xFD, 0xE7 }, { 0x00, 0x04, 0x08 }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat8309000409",   EC_M,    EC_M, { 0x4B, 0xD3, 0x07, 0x96, 0x49, 0xF9, 0x2D }, { 0x00, 0x04, 0x09 }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat830900040A",   EC_M,    EC_M, { 0xEE, 0xED, 0xDF, 0x3C, 0xB6, 0x35, 0x4E }, { 0x00, 0x04, 0x0A }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat830900040B",   EC_M,    EC_M, { 0xA5, 0x56, 0x32, 0x10, 0x25, 0xD7, 0xC8 }, { 0x00, 0x04, 0x0B }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat830900040D",   EC_M,    EC_M, { 0x45, 0x08, 0x95, 0xFF, 0xED, 0x45, 0x21 }, { 0x00, 0x04, 0x0D }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat830900040E",   EC_M,    EC_M, { 0x25, 0x89, 0x41, 0xEA, 0x45, 0xFE, 0x00 }, { 0x00, 0x04, 0x0E }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat830900040F",   EC_M,    EC_M, { 0xD1, 0x2A, 0x47, 0xE7, 0x85, 0x4F, 0xFE }, { 0x00, 0x04, 0x0F }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat8309000418",   EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat8309000419",   EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x04, 0x19 }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat830900041C",   EC_M,    EC_M, { 0x69, 0x36, 0x22, 0xCB, 0x33, 0xF3, 0x13 }, { 0x00, 0x04, 0x1C }, { "06/03/1995" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat0758000418",   EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "09/01/1996" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat0758000419",   EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x04, 0x19 }, { "09/01/1996" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat075800041C",   EC_M,    EC_M, { 0x2C, 0xBE, 0x80, 0x40, 0x30, 0x64, 0xA4 }, { 0x00, 0x04, 0x1C }, { "09/01/1996" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat3955000418",   EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "01/10/1999" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat3955000419",   EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x04, 0x19 }, { "01/10/1999" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat395500041C",   EC_M,    EC_M, { 0x2C, 0xBE, 0x80, 0x40, 0x30, 0x64, 0xA4 }, { 0x00, 0x04, 0x1C }, { "01/10/1999" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat2796000418",   EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "02/11/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat2796000419",   EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x04, 0x19 }, { "02/11/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat279600041C",   EC_M,    EC_M, { 0x2C, 0xBE, 0x80, 0x40, 0x30, 0x64, 0xA4 }, { 0x00, 0x04, 0x1C }, { "02/11/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat2203000408",   EC_M,    EC_M, { 0x73, 0x13, 0xC6, 0x8A, 0x35, 0xFD, 0xE7 }, { 0x00, 0x04, 0x08 }, { "01/12/2001" }, { 0xFF, 0x00 }, "TV3 (M)" },
	{ "viasat220300040A",   EC_M,    EC_M, { 0xE5, 0x94, 0xB1, 0x1C, 0x3C, 0x3F, 0xE4 }, { 0x00, 0x04, 0x0A }, { "01/12/2001" }, { 0xFF, 0x00 }, "TV3 (M)" },
	{ "viasat220300040E",   EC_M,    EC_M, { 0xB2, 0x41, 0x37, 0x4F, 0xA4, 0x5B, 0x34 }, { 0x00, 0x04, 0x0E }, { "01/12/2001" }, { 0xFF, 0x00 }, "TV3 (M)" },
	{ "viasat2203000418",   EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "29/03/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat2203000419",   EC_M,    EC_M, { 0x69, 0x36, 0x22, 0xCB, 0x33, 0xF3, 0x13 }, { 0x00, 0x04, 0x19 }, { "29/03/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat220300040CAU", EC_M,    EC_M, { 0xE9, 0xF3, 0x34, 0x36, 0xB0, 0xBB, 0xF8 }, { 0x00, 0x04, 0x0C }, { "TODAY"      }, { 0xFF, 0x10 }, "TV3 (AU - M)" },
	{ "viasat220300041CAU", EC_M,    EC_M, { 0xE9, 0xF3, 0x34, 0x36, 0xB0, 0xBB, 0xF8 }, { 0x00, 0x04, 0x1C }, { "TODAY"      }, { 0xFF, 0x0A }, "TV1000 (AU - M)" },
        { "viasat2508000418",   EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "11/06/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat2508000419",   EC_M,    EC_M, { 0xB4, 0xB9, 0xCB, 0xAF, 0x30, 0x2F, 0xFE }, { 0x00, 0x04, 0x19 }, { "11/06/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat250800041A",   EC_M,    EC_M, { 0x17, 0x0D, 0x10, 0xDF, 0x6A, 0x66, 0x85 }, { 0x00, 0x04, 0x1A }, { "11/06/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat250800041B",   EC_M,    EC_M, { 0xD6, 0xD8, 0xB9, 0x2E, 0x38, 0x1A, 0xDA }, { 0x00, 0x04, 0x1B }, { "11/06/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat250800041C",   EC_M,    EC_M, { 0x2C, 0xBE, 0x80, 0x40, 0x30, 0x64, 0xA4 }, { 0x00, 0x04, 0x1C }, { "11/06/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat250800041D",   EC_M,    EC_M, { 0x93, 0x0C, 0x4E, 0x42, 0x16, 0xF5, 0xFE }, { 0x00, 0x04, 0x1D }, { "11/06/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat250800041E",   EC_M,    EC_M, { 0x27, 0x15, 0x11, 0xCD, 0xB0, 0xD2, 0x0D }, { 0x00, 0x04, 0x1E }, { "11/06/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat250800041F",   EC_M,    EC_M, { 0x6F, 0x3F, 0x18, 0x51, 0x89, 0xA2, 0xFB }, { 0x00, 0x04, 0x1F }, { "11/06/1998" }, { 0xFF, 0x00 }, "TV1000 (M)" },
        { "viasat6390000418",   EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "15/02/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat6390000419",   EC_M,    EC_M, { 0x69, 0x36, 0x22, 0xCB, 0x33, 0xF3, 0x13 }, { 0x00, 0x04, 0x19 }, { "15/02/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat639000041A",   EC_M,    EC_M, { 0x45, 0x70, 0x36, 0x8B, 0x64, 0x99, 0xF7 }, { 0x00, 0x04, 0x1A }, { "15/02/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat639000041B",   EC_M,    EC_M, { 0xD6, 0xD8, 0xB9, 0x2E, 0x38, 0x1A, 0xDA }, { 0x00, 0x04, 0x1B }, { "15/02/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat639000041C",   EC_M,    EC_M, { 0x2C, 0xBE, 0x80, 0x40, 0x30, 0x64, 0xA4 }, { 0x00, 0x04, 0x1C }, { "15/02/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat639000041D",   EC_M,    EC_M, { 0x93, 0x0C, 0x4E, 0x42, 0x16, 0xF5, 0xFE }, { 0x00, 0x04, 0x1D }, { "15/02/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat639000041E",   EC_M,    EC_M, { 0x27, 0x15, 0x11, 0xCD, 0xB0, 0xD2, 0x0D }, { 0x00, 0x04, 0x1E }, { "15/02/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat639000041F",   EC_M,    EC_M, { 0x6F, 0x3F, 0x18, 0x51, 0x89, 0xA2, 0xFB }, { 0x00, 0x04, 0x1F }, { "15/02/2000" }, { 0xFF, 0x00 }, "TV1000 (M)" },	
	{ "viasat4465000418",   EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x18 }, { "19/09/1996" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "viasat4465000419",   EC_M,    EC_M, { 0x48, 0x63, 0xC5, 0xB3, 0xDA, 0xE3, 0x29 }, { 0x00, 0x04, 0x19 }, { "19/09/1996" }, { 0xFF, 0x00 }, "TV1000 (M)" },
	{ "filmnet7869002808",  EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "26/06/1994" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet7869002809",  EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x28, 0x09 }, { "26/06/1994" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet786900280A",  EC_M,    EC_M, { 0xF2, 0x58, 0x27, 0x33, 0xE5, 0x79, 0x61 }, { 0x00, 0x28, 0x0A }, { "26/06/1994" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet786900280B",  EC_M,    EC_M, { 0x5E, 0xEC, 0xF3, 0x5B, 0x54, 0x6E, 0x88 }, { 0x00, 0x28, 0x0B }, { "26/06/1994" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet786900280C",  EC_M,    EC_M, { 0xCD, 0xEF, 0x73, 0x53, 0x4E, 0x7D, 0x94 }, { 0x00, 0x28, 0x0C }, { "26/06/1994" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet786900280D",  EC_M,    EC_M, { 0x35, 0x5B, 0x37, 0x04, 0xBB, 0x44, 0x0E }, { 0x00, 0x28, 0x0D }, { "26/06/1994" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet4378002808",  EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "25/02/1996" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet4378002809",  EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x28, 0x09 }, { "25/02/1996" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet8754002808",  EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "26/11/1996" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet8754002809",  EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x28, 0x09 }, { "26/11/1996" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet4528002808",  EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "27/03/1996" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet4528002808AU",EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "TODAY"      }, { 0xFF, 0x00 }, "Filmnet (AU - M)" },
	{ "filmnet2508002808",  EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "27/03/1996" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet2508002809",  EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x28, 0x09 }, { "27/03/1996" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet5018002808",  EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "27/03/1996" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet5018002809",  EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x28, 0x09 }, { "27/03/1996" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet4859002808",  EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "28/08/1992" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet4859002809",  EC_M,    EC_M, { 0x33, 0x22, 0x44, 0x53, 0xFF, 0x58, 0x50 }, { 0x00, 0x28, 0x09 }, { "28/08/1992" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "filmnet7167002808",  EC_M,    EC_M, { 0x21, 0x12, 0x31, 0x35, 0x8A, 0xC3, 0x4F }, { 0x00, 0x28, 0x08 }, { "27/12/1994" }, { 0xFF, 0x00 }, "FilmNet (M)" },
	{ "CD0A7890",           EC_S,    EC_S, { 0XA9, 0X91, 0X86, 0X2C, 0X90, 0X6B, 0X9A }, { 0x00, 0x2B, 0x10 }, { "03/12/1998" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD1A7890",           EC_S,    EC_S, { 0x07, 0x3A, 0x0F, 0xB1, 0x4E, 0x49, 0x6D }, { 0x00, 0x2B, 0x11 }, { "03/12/1998" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD2A7890",           EC_S,    EC_S, { 0xA3, 0x53, 0x0C, 0x12, 0x55, 0xA3, 0x59 }, { 0x00, 0x2B, 0x12 }, { "03/12/1998" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD3A7890",           EC_S,    EC_S, { 0x76, 0xBF, 0x8E, 0xF4, 0xA1, 0x65, 0xB4 }, { 0x00, 0x2B, 0x13 }, { "03/12/1998" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CDDDA7890",          EC_3DES, EC_3DES, { 0x07, 0x3A, 0x0F, 0xB1, 0x4E, 0x49, 0x6D,   /* Key 01 and key 02 - index key D */
	                               0xA3, 0x53, 0x0C, 0x12, 0x55, 0xA3, 0x59 }, { 0x00, 0x2B, 0x1D }, { "03/12/1998" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "CDDEA7890",          EC_3DES, EC_3DES, { 0xA3, 0x53, 0x0C, 0x12, 0x55, 0xA3, 0x59,   /* Key 02 and key 03 - index key E */
	                               0x76, 0xBF, 0x8E, 0xF4, 0xA1, 0x65, 0xB4 }, { 0x00, 0x2B, 0x1E }, { "03/12/1998" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "CD0B0286",           EC_S,    EC_S, { 0XA9, 0X91, 0X86, 0X2C, 0X90, 0X6B, 0X9A }, { 0x00, 0x2B, 0x10 }, { "25/08/1998" }, { 0xFF, 0x00 }, "Canal+ (S2)" },                               
	{ "CD1B0286",           EC_S,    EC_S, { 0XA9, 0X1B, 0X08, 0X0E, 0XFE, 0X69, 0XD6 }, { 0x00, 0x2B, 0x11 }, { "25/08/1998" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD2B0286",           EC_S,    EC_S, { 0X71, 0X77, 0X7D, 0X16, 0X54, 0X71, 0XA3 }, { 0x00, 0x2B, 0x12 }, { "25/08/1998" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD3B0286",           EC_S,    EC_S, { 0X07, 0XA3, 0X6C, 0XF8, 0X64, 0X37, 0XF4 }, { 0x00, 0x2B, 0x13 }, { "25/08/1998" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CDDDB0286",          EC_3DES, EC_3DES, { 0XA9, 0X1B, 0X08, 0X0E, 0XFE, 0X69, 0XD6,   /* Key 01 and key 02 - index key D */
	                               0X71, 0X77, 0X7D, 0X16, 0X54, 0X71, 0XA3 }, { 0x00, 0x2B, 0x1D }, { "25/08/1998" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "CDDEB0286",          EC_3DES, EC_3DES, { 0xA3, 0x53, 0x0C, 0x12, 0x55, 0xA3, 0x59,   /* Key 02 and key 03 - index key E */
	                               0x76, 0xBF, 0x8E, 0xF4, 0xA1, 0x65, 0xB4 }, { 0x00, 0x2B, 0x1E }, { "25/08/1998" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "CD0C4972",           EC_S,    EC_S, { 0XA9, 0X91, 0X86, 0X2C, 0X90, 0X6B, 0X9A }, { 0x00, 0x2B, 0x10 }, { "28/09/1999" }, { 0xFF, 0x00 }, "Canal+ (S2)" },                               
	{ "CD1C4972",           EC_S,    EC_S, { 0XA9, 0X1B, 0X08, 0X0E, 0XFE, 0X69, 0XD6 }, { 0x00, 0x2B, 0x11 }, { "28/09/1999" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD2C4972",           EC_S,    EC_S, { 0X71, 0X77, 0X7D, 0X16, 0X54, 0X71, 0XA3 }, { 0x00, 0x2B, 0x12 }, { "28/09/1999" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD3C4972",           EC_S,    EC_S, { 0X07, 0XA3, 0X6C, 0XF8, 0X64, 0X37, 0XF4 }, { 0x00, 0x2B, 0x13 }, { "28/09/1999" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CDDDC4972",          EC_3DES, EC_3DES, { 0XA9, 0X1B, 0X08, 0X0E, 0XFE, 0X69, 0XD6,   /* Key 01 and key 02 - index key D */
	                               0X71, 0X77, 0X7D, 0X16, 0X54, 0X71, 0XA3 }, { 0x00, 0x2B, 0x1D }, { "28/09/1999" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "CDDEC4972",          EC_3DES, EC_3DES, { 0xA3, 0x53, 0x0C, 0x12, 0x55, 0xA3, 0x59,   /* Key 02 and key 03 - index key E */
	                               0x76, 0xBF, 0x8E, 0xF4, 0xA1, 0x65, 0xB4 }, { 0x00, 0x2B, 0x1E }, { "28/09/1999" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "CD0D9375",           EC_S,    EC_S, { 0XA9, 0X91, 0X86, 0X2C, 0X90, 0X6B, 0X9A }, { 0x00, 0x2B, 0x10 }, { "12/01/1999" }, { 0xFF, 0x00 }, "Canal+ (S2)" },                               
	{ "CD1D9375",           EC_S,    EC_S, { 0XA9, 0X1B, 0X08, 0X0E, 0XFE, 0X69, 0XD6 }, { 0x00, 0x2B, 0x11 }, { "12/01/1999" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD2D9375",           EC_S,    EC_S, { 0X71, 0X77, 0X7D, 0X16, 0X54, 0X71, 0XA3 }, { 0x00, 0x2B, 0x12 }, { "12/01/1999" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD3D9375",           EC_S,    EC_S, { 0X07, 0XA3, 0X6C, 0XF8, 0X64, 0X37, 0XF4 }, { 0x00, 0x2B, 0x13 }, { "12/01/1999" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CDDDD9375",          EC_3DES, EC_3DES, { 0XA9, 0X1B, 0X08, 0X0E, 0XFE, 0X69, 0XD6,   /* Key 01 and key 02 - index key D */
	                               0X71, 0X77, 0X7D, 0X16, 0X54, 0X71, 0XA3 }, { 0x00, 0x2B, 0x1D }, { "12/01/1999" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "CDDED9375",          EC_3DES, EC_3DES, { 0xA3, 0x53, 0x0C, 0x12, 0x55, 0xA3, 0x59,   /* Key 02 and key 03 - index key E */
	                               0x76, 0xBF, 0x8E, 0xF4, 0xA1, 0x65, 0xB4 }, { 0x00, 0x2B, 0x1E }, { "12/01/1999" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "CD0E0170",           EC_S,    EC_S, { 0XA9, 0X91, 0X86, 0X2C, 0X90, 0X6B, 0X9A }, { 0x00, 0x2B, 0x10 }, { "21/09/2000" }, { 0xFF, 0x00 }, "Canal+ (S2)" },                               
	{ "CD1E0170",           EC_S,    EC_S, { 0XA9, 0X1B, 0X08, 0X0E, 0XFE, 0X69, 0XD6 }, { 0x00, 0x2B, 0x11 }, { "21/09/2000" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD2E0170",           EC_S,    EC_S, { 0X71, 0X77, 0X7D, 0X16, 0X54, 0X71, 0XA3 }, { 0x00, 0x2B, 0x12 }, { "21/09/2000" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CD3E0170",           EC_S,    EC_S, { 0X07, 0XA3, 0X6C, 0XF8, 0X64, 0X37, 0XF4 }, { 0x00, 0x2B, 0x13 }, { "21/09/2000" }, { 0xFF, 0x00 }, "Canal+ (S2)" },
	{ "CDDDE0170",          EC_3DES, EC_3DES, { 0XA9, 0X1B, 0X08, 0X0E, 0XFE, 0X69, 0XD6,   /* Key 01 and key 02 - index key D */
	                               0X71, 0X77, 0X7D, 0X16, 0X54, 0X71, 0XA3 }, { 0x00, 0x2B, 0x1D }, { "21/09/2000" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "CDDEE0170",          EC_3DES, EC_3DES, { 0xA3, 0x53, 0x0C, 0x12, 0x55, 0xA3, 0x59,   /* Key 02 and key 03 - index key E */
	                               0x76, 0xBF, 0x8E, 0xF4, 0xA1, 0x65, 0xB4 }, { 0x00, 0x2B, 0x1E }, { "21/09/2000" }, { 0xFF, 0x00 }, "Canal+ (3DES)" },
	{ "cplusfr43AU",        EC_M,    EC_M, { 0x69, 0x41, 0x2D, 0x4C, 0x56, 0x28, 0xCF }, { 0x10, 0x00, 0x18 }, { "TODAY"      }, { 0xFF, 0x00 }, "Canal+ 4/3 (M)" },
	{ "cplusfr169AU",       EC_M,    EC_M, { 0xEC, 0xA6, 0xE8, 0x4E, 0x10, 0x41, 0x6F }, { 0x10, 0x00, 0x28 }, { "TODAY"      }, { 0xFF, 0x00 }, "Canal+ 16/9 (M)" },
	{ "cinecfrAU",          EC_M,    EC_M, { 0xEC, 0xA6, 0xE8, 0x4E, 0x10, 0x41, 0x6F }, { 0x10, 0x00, 0x38 }, { "TODAY"      }, { 0xFF, 0x00 }, "Cine Cinemas (M)" },
	{ "cplusfr43AUABSAT",   EC_M,    EC_M, { 0x69, 0x41, 0x2D, 0x4C, 0x56, 0x28, 0xCF }, { 0x10, 0x00, 0x18 }, { "TODAY"      }, { 0xFF, 0x00 }, "Canal+ 4/3 (M)" },
	{ NULL } 
};

/* Data for EC controlled-access EMMs */
const static em_mode_t _em_modes[] = {
	{ "tv3",       	  EC_M,    EC_M, { 0x99, 0xCF, 0xCA, 0x13, 0x7A, 0x53, 0x6D }, { 0x00, 0x04, 0x04 }, { 0x70, 0x31, 0x12 }, { 0xF0, 0xF0, 0xF0, 0xF0, 0xF0 }, EMMU },
	{ "visiopass",    EC_M,    EC_M, { 0x3B, 0xDD, 0x2C, 0xF3, 0xC1, 0xA0, 0x03 }, { 0x00, 0x24, 0x03 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "cplusfr",	  EC_M,    EC_M, { 0xB2, 0x26, 0xF7, 0x98, 0x36, 0xEB, 0xC8 }, { 0x10, 0x00, 0x13 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "tvcable",	  EC_M,    EC_M, { 0x28, 0x99, 0x6E, 0x7D, 0x15, 0x6B, 0x6B }, { 0x00, 0x0C, 0x02 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "cplusfr169",   EC_M,    EC_M, { 0x6B, 0xB7, 0x78, 0x65, 0xA8, 0xC7, 0xF2 }, { 0x10, 0x00, 0x23 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "filmnet",   	  EC_M,    EC_M, { 0x13, 0x39, 0x6F, 0xDB, 0x3A, 0x88, 0x60 }, { 0x00, 0x28, 0x06 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "ctv",       	  EC_M,    EC_M, { 0x5E, 0xC9, 0xD7, 0x19, 0x89, 0x64, 0xE6 }, { 0x00, 0x04, 0x34 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMS },
	{ "ctvs",      EC_3DES,    EC_S, { 0x64, 0x34, 0xF4, 0x14, 0x0B, 0x01, 0x00,
	                            	   0xA3, 0x8E, 0xEF, 0x1A, 0x65, 0x6A, 0x2C }, { 0x00, 0x2B, 0x20 }, { 0x80, 0x09, 0x3D }, { 0xA1, 0x6C, 0xAF, 0x2F, 0x00 }, EMMU },
	{ "cplus",     EC_3DES, EC_3DES, { 0x10, 0xDD, 0x38, 0x71, 0xCB, 0xCB, 0x6A,
	                            	   0x52, 0xA8, 0x54, 0xA1, 0xA7, 0xE5, 0x0D }, { 0x00, 0x2B, 0x00 }, { 0x80, 0x09, 0x3D }, { 0xA1, 0x6C, 0xAF, 0x2F, 0x00 }, EMMS },
	{ "tvplus",       EC_M,    EC_M, { 0x21, 0xF5, 0x50, 0xAC, 0x0E, 0xF4, 0xA7 }, { 0x00, 0x2C, 0x03 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "btv",          EC_M,    EC_M, { 0xBB, 0x2D, 0x2E, 0xC5, 0x50, 0x28, 0x1E }, { 0x00, 0x2C, 0x43 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "rdv",          EC_S,    EC_S, { 0x13, 0x39, 0x6F, 0xDB, 0x3A, 0x88, 0x60 }, { 0x00, 0x2d, 0x80 }, { 0x70, 0x31, 0x12 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMS },
	{ "tv2",       EC_3DES,    EC_M, { 0xEA, 0xC5, 0xE3, 0x45, 0x8E, 0x82, 0xCE,
	                            	   0x83, 0x2A, 0x98, 0x4C, 0xD9, 0xBA, 0x31 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x3E, 0xE3, 0x8E, 0x06, 0x00 }, EMMU },
	{ "viasat220300040CAU",    EC_M, EC_M, { 0x98, 0x44, 0x0A, 0x05, 0x9B, 0x4A, 0xF7 }, { 0x00, 0x04, 0x04 }, { 0x21, 0x7B, 0x90 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "viasat220300041CAU",    EC_M, EC_M, { 0x0B, 0xEC, 0x0E, 0x83, 0xE5, 0x0B, 0xC8 }, { 0x00, 0x04, 0x14 }, { 0x44, 0x21, 0x7B }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "filmnet4528002808AU",   EC_M, EC_M, { 0x41, 0xC1, 0xA2, 0x69, 0x6B, 0xEE, 0x23 }, { 0x00, 0x28, 0x00 }, { 0x04, 0x34, 0x42 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "tv22866475200AU",       EC_S, EC_S, { 0x70, 0xBF, 0x6E, 0x51, 0x9F, 0xB8, 0xA6 }, { 0x47, 0x51, 0x00 }, { 0x09, 0x27, 0xCE }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "cplusfr43AU",           EC_M, EC_M, { 0x39, 0x74, 0xD7, 0xC1, 0x3F, 0x1B, 0x0D }, { 0x10, 0x00, 0x15 }, { 0x04, 0x93, 0xF2 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "cplusfr169AU",          EC_M, EC_M, { 0x12, 0x37, 0x5A, 0x43, 0xE4, 0xA1, 0x48 }, { 0x10, 0x00, 0x26 }, { 0x04, 0x93, 0xF2 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "cinecfrAU",             EC_M, EC_M, { 0x89, 0x61, 0x30, 0x3B, 0x3F, 0x7B, 0xA8 }, { 0x10, 0x00, 0x36 }, { 0x04, 0x93, 0xF2 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ "cplusfr43AUABSAT",      EC_M, EC_M, { 0x39, 0x74, 0xD7, 0xC1, 0x3F, 0x1B, 0x0D }, { 0x10, 0x00, 0x15 }, { 0x04, 0x93, 0xF2 }, { 0x00, 0x00, 0x00, 0x00, 0x00 }, EMMG },
	{ NULL } 
};

/* Initial permutation for Eurocrypt-S2/3DES */
static const uint8_t _ip[] = {
	58, 50, 42, 34, 26, 18, 10, 2,
	60, 52, 44, 36, 28, 20, 12, 4,
	62, 54, 46, 38, 30, 22, 14, 6,
	64, 56, 48, 40, 32, 24, 16, 8,
	57, 49, 41, 33, 25, 17,  9, 1,
	59, 51, 43, 35, 27, 19, 11, 3,
	61, 53, 45, 37, 29, 21, 13, 5,
	63, 55, 47, 39, 31, 23, 15, 7,
};

/* Inverse/final permutation for Eurocrypt-S2/3DES */
static const uint8_t _ipp[] = {
	40, 8, 48, 16, 56, 24, 64, 32,
	39, 7, 47, 15, 55, 23, 63, 31,
	38, 6, 46, 14, 54, 22, 62, 30,
	37, 5, 45, 13, 53, 21, 61, 29,
	36, 4, 44, 12, 52, 20, 60, 28,
	35, 3, 43, 11, 51, 19, 59, 27,
	34, 2, 42, 10, 50, 18, 58, 26,
	33, 1, 41,  9, 49, 17, 57, 25,
};

static const uint8_t _exp[] = {
	32,  1,  2,  3,  4,  5,
	 4,  5,  6,  7,  8,  9,
	 8,  9, 10, 11, 12, 13,
	12, 13, 14, 15, 16, 17,
	16, 17, 18, 19, 20, 21,
	20, 21, 22, 23, 24, 25,
	24, 25, 26, 27, 28, 29,
	28, 29, 30, 31, 32,  1
};

static const uint8_t _sb[][64] = {
	{ 0xE, 0x0, 0x4, 0xF, 0xD, 0x7, 0x1, 0x4,
	  0x2, 0xE, 0xF, 0x2, 0xB, 0xD, 0x8, 0x1,
	  0x3, 0xA, 0xA, 0x6, 0x6, 0xC, 0xC, 0xB,
	  0x5, 0x9, 0x9, 0x5, 0x0, 0x3, 0x7, 0x8,
	  0x4, 0xF, 0x1, 0xC, 0xE, 0x8, 0x8, 0x2,
	  0xD, 0x4, 0x6, 0x9, 0x2, 0x1, 0xB, 0x7,
	  0xF, 0x5, 0xC, 0xB, 0x9, 0x3, 0x7, 0xE,
	  0x3, 0xA, 0xA, 0x0, 0x5, 0x6, 0x0, 0xD
	},
	{ 0xF, 0x3, 0x1, 0xD, 0x8, 0x4, 0xE, 0x7,
	  0x6, 0xF, 0xB, 0x2, 0x3, 0x8, 0x4, 0xE,
	  0x9, 0xC, 0x7, 0x0, 0x2, 0x1, 0xD, 0xA,
	  0xC, 0x6, 0x0, 0x9, 0x5, 0xB, 0xA, 0x5,
	  0x0, 0xD, 0xE, 0x8, 0x7, 0xA, 0xB, 0x1,
	  0xA, 0x3, 0x4, 0xF, 0xD, 0x4, 0x1, 0x2,
	  0x5, 0xB, 0x8, 0x6, 0xC, 0x7, 0x6, 0xC,
	  0x9, 0x0, 0x3, 0x5, 0x2, 0xE, 0xF, 0x9
	},
	{ 0xA, 0xD, 0x0, 0x7, 0x9, 0x0, 0xE, 0x9,
	  0x6, 0x3, 0x3, 0x4, 0xF, 0x6, 0x5, 0xA,
	  0x1, 0x2, 0xD, 0x8, 0xC, 0x5, 0x7, 0xE,
	  0xB, 0xC, 0x4, 0xB, 0x2, 0xF, 0x8, 0x1,
	  0xD, 0x1, 0x6, 0xA, 0x4, 0xD, 0x9, 0x0,
	  0x8, 0x6, 0xF, 0x9, 0x3, 0x8, 0x0, 0x7,
	  0xB, 0x4, 0x1, 0xF, 0x2, 0xE, 0xC, 0x3,
	  0x5, 0xB, 0xA, 0x5, 0xE, 0x2, 0x7, 0xC
	},
	{ 0x7, 0xD, 0xD, 0x8, 0xE, 0xB, 0x3, 0x5,
	  0x0, 0x6, 0x6, 0xF, 0x9, 0x0, 0xA, 0x3,
	  0x1, 0x4, 0x2, 0x7, 0x8, 0x2, 0x5, 0xC,
	  0xB, 0x1, 0xC, 0xA, 0x4, 0xE, 0xF, 0x9,
	  0xA, 0x3, 0x6, 0xF, 0x9, 0x0, 0x0, 0x6,
	  0xC, 0xA, 0xB, 0x1, 0x7, 0xD, 0xD, 0x8,
	  0xF, 0x9, 0x1, 0x4, 0x3, 0x5, 0xE, 0xB,
	  0x5, 0xC, 0x2, 0x7, 0x8, 0x2, 0x4, 0xE
	},
	{ 0x2, 0xE, 0xC, 0xB, 0x4, 0x2, 0x1, 0xC,
	  0x7, 0x4, 0xA, 0x7, 0xB, 0xD, 0x6, 0x1,
	  0x8, 0x5, 0x5, 0x0, 0x3, 0xF, 0xF, 0xA,
	  0xD, 0x3, 0x0, 0x9, 0xE, 0x8, 0x9, 0x6,
	  0x4, 0xB, 0x2, 0x8, 0x1, 0xC, 0xB, 0x7,
	  0xA, 0x1, 0xD, 0xE, 0x7, 0x2, 0x8, 0xD,
	  0xF, 0x6, 0x9, 0xF, 0xC, 0x0, 0x5, 0x9,
	  0x6, 0xA, 0x3, 0x4, 0x0, 0x5, 0xE, 0x3
	},
	{ 0xC, 0xA, 0x1, 0xF, 0xA, 0x4, 0xF, 0x2,
	  0x9, 0x7, 0x2, 0xC, 0x6, 0x9, 0x8, 0x5,
	  0x0, 0x6, 0xD, 0x1, 0x3, 0xD, 0x4, 0xE,
	  0xE, 0x0, 0x7, 0xB, 0x5, 0x3, 0xB, 0x8,
	  0x9, 0x4, 0xE, 0x3, 0xF, 0x2, 0x5, 0xC,
	  0x2, 0x9, 0x8, 0x5, 0xC, 0xF, 0x3, 0xA,
	  0x7, 0xB, 0x0, 0xE, 0x4, 0x1, 0xA, 0x7,
	  0x1, 0x6, 0xD, 0x0, 0xB, 0x8, 0x6, 0xD
	},
	{ 0x4, 0xD, 0xB, 0x0, 0x2, 0xB, 0xE, 0x7,
	  0xF, 0x4, 0x0, 0x9, 0x8, 0x1, 0xD, 0xA,
	  0x3, 0xE, 0xC, 0x3, 0x9, 0x5, 0x7, 0xC,
	  0x5, 0x2, 0xA, 0xF, 0x6, 0x8, 0x1, 0x6,
	  0x1, 0x6, 0x4, 0xB, 0xB, 0xD, 0xD, 0x8,
	  0xC, 0x1, 0x3, 0x4, 0x7, 0xA, 0xE, 0x7,
	  0xA, 0x9, 0xF, 0x5, 0x6, 0x0, 0x8, 0xF,
	  0x0, 0xE, 0x5, 0x2, 0x9, 0x3, 0x2, 0xC
	},
	{ 0xD, 0x1, 0x2, 0xF, 0x8, 0xD, 0x4, 0x8,
	  0x6, 0xA, 0xF, 0x3, 0xB, 0x7, 0x1, 0x4,
	  0xA, 0xC, 0x9, 0x5, 0x3, 0x6, 0xE, 0xB,
	  0x5, 0x0, 0x0, 0xE, 0xC, 0x9, 0x7, 0x2,
	  0x7, 0x2, 0xB, 0x1, 0x4, 0xE, 0x1, 0x7,
	  0x9, 0x4, 0xC, 0xA, 0xE, 0x8, 0x2, 0xD,
	  0x0, 0xF, 0x6, 0xC, 0xA, 0x9, 0xD, 0x0,
	  0xF, 0x3, 0x3, 0x5, 0x5, 0x6, 0x8, 0xB
	}
};

static const uint8_t _perm[] = {
	16,  7, 20, 21,
	29, 12, 28, 17,
	 1, 15, 23, 26,
	 5, 18, 31, 10,
	 2,  8, 24, 14,
	32, 27,  3,  9,
	19, 13, 30,  6,
	22, 11,  4, 25
};

/* Inverse PC1 table */
static const uint8_t _ipc1[] = {
	8, 16, 24, 56, 52, 44, 36, 57, 
	7, 15, 23, 55, 51, 43, 35, 58, 
	6, 14, 22, 54, 50, 42, 34, 59, 
	5, 13, 21, 53, 49, 41, 33, 60, 
	4, 12, 20, 28, 48, 40, 32, 61, 
	3, 11, 19, 27, 47, 39, 31, 62, 
	2, 10, 18, 26, 46, 38, 30, 63, 
	1,  9, 17, 25, 45, 37, 29, 64
};

static const uint8_t _pc2[] = {
	14, 17, 11, 24,  1,  5,
	 3, 28, 15,  6, 21, 10,
	23, 19, 12,  4, 26,  8,
	16,  7, 27, 20, 13,  2,
	41, 52, 31, 37, 47, 55,
	30, 40, 51, 45, 33, 48,
	44, 49, 39, 56, 34, 53,
	46, 42, 50, 36, 29, 32
};

/* Triple DES key map table */
static const uint8_t _tdesmap[4][2] = {
	{ 0x00, 0x01 }, /* Index C */
	{ 0x01, 0x02 }, /* Index D */
	{ 0x02, 0x03 }, /* Index E */
	{ 0x03, 0x00 }  /* Index F */
};

static const uint8_t _lshift[] = {
	1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1
};

static uint8_t flag = 0;

static void _permute_ec(uint8_t *data, const uint8_t *pr, int n)
{
	uint8_t pin[8];
	int i, j, k, t;
	
	for(i = 0, k = 0; k < n; i++)
	{
		int p = 0;
		
		for(j = 7; j >= 0; j--, k++)
		{
			t = pr[k] - 1;
			p = (p << 1) + ((data[t >> 3]) >> (7 - (t & 7)) & 1);
		}
		
		pin[i] = p;
	}
	
	memcpy(data, pin, 8);
}

uint16_t _get_ec_date(const char *dtm, int mode)
{
	int day, mon, year;
	
	uint16_t date;
	
	sscanf(dtm, "%d/%d/%d", &day, &mon, &year);
	
	/* EC-M and EC-S2/3DES have different date byte structures */
	if(mode == EC_M)
	{
		date  = (year - 1980) << 9; /* Year - first 7 bits */
		date |= mon << 5;           /* Month - next 4 bits */
		date |= day << 0;           /* Day - next 5 bits */
	}
	else
	{
		date  = (year - 1990) << 12; /* Year - first 4 bits */
		date |= mon << 8;            /* Month - next 4 bits */
		date |= day << 0;            /* Day - 8 bits */
	}
	
	return (date);
}

static uint64_t _ec_des_f(uint64_t r, uint8_t *k2)
{
	int i, k;
	uint64_t s = 0, result = 0;
	
	for(i = 0, k = 0; i < 8; i++)
	{
		int j;
		uint8_t v = 0;
		
		/* The expansion E (R1) */
		for(j = 0; j < 6; j++, k++)
		{
			v |= (r >> (32 - _exp[k]) & 1) << (5 - j);
		}
		
		/* Create R2 */
		v ^= k2[i];
		
		/* The S-boxes */
		s |= (uint64_t) _sb[i][v] << (28 - 4 * i);
	}
	
	/* The permutation P (R3) */
	for(i = 0; i < 32; i++)
	{
		result |= (s >> (32 - _perm[i]) & 1) << (31 - i);
	}
	
	return(result);
}

static void _key_rotate_ec(uint64_t *c, uint64_t *d, int dir, int iter)
{
	int i;
	
	/* Rotate left (decryption) */
	if(dir == ROTATE_LEFT)
	{
		for(i = 0; i < _lshift[iter]; i++)
		{
			*c = ((*c << 1) ^ (*c >> 27)) & 0xFFFFFFFL;
			*d = ((*d << 1) ^ (*d >> 27)) & 0xFFFFFFFL;
		}
	}
	/* Rotate right (encryption) */
	else
	{
		for(i = 0; i < _lshift[15 - iter]; i++)
		{
			*c = ((*c >> 1) ^ (*c << 27)) & 0xFFFFFFFL;
			*d = ((*d >> 1) ^ (*d << 27)) & 0xFFFFFFFL;
		}
	}
}

static void _key_exp(uint64_t *c, uint64_t *d, uint8_t *k2)
{
	int j, k;
	
	/* Key expansion */
	for(j = 0, k = 0; j < 8; j++)
	{
		int t;
		
		k2[j] = 0;
		
		for(t = 0; t < 6; t++, k++)
		{
			if(_pc2[k] < 29)
			{
				k2[j] |= (*c >> (28 - _pc2[k]) & 1) << (5 - t);
			}
			else
			{
				k2[j] |= (*d >> (56 - _pc2[k]) & 1) << (5 - t);
			}
		}
	}
}

static void _eurocrypt(uint8_t *data, const uint8_t *key, int desmode, int emode, int rnd)
{
	int i;
	uint64_t r, l, c, d, s;
	
	/* Key preparation. Split key into two halves */
	c = ((uint64_t) key[0] << 20)
	  ^ ((uint64_t) key[1] << 12)
	  ^ ((uint64_t) key[2] << 4)
	  ^ ((uint64_t) key[3] >> 4);
	
	d = ((uint64_t) (key[3] & 0x0F) << 24)
	  ^ ((uint64_t) key[4] << 16)
	  ^ ((uint64_t) key[5] << 8)
	  ^ ((uint64_t) key[6] << 0);
	
	/* Initial permutation for Eurocrypt S2/3DES  - always do this */
	if(emode != EC_M)
	{
		_permute_ec(data, _ip, 64);
	}
	
	/* Control word preparation. Split CW into two halves. */
	for(i = 3, l = 0, r = 0; i >= 0; i--)
	{
		l ^= (uint64_t) data[3 - i] << (8 * i);
		r ^= (uint64_t) data[7 - i] << (8 * i);
	}
	
	/* 16 iterations */
	for(i = 0; i < 16; i++)
	{
		uint64_t r3;
		uint8_t k2[8];
		
		switch (emode) {
			/* If mode is not valid, abort -- this is a bug! */
			default:
				fprintf(stderr, "_eurocrypt: BUG: invalid encryption mode!!!\n");
				assert(0);
				break;

			/* EC-M */
			case EC_M:
				{
					if(desmode == HASH)
					{
						_key_rotate_ec(&c, &d, ROTATE_LEFT, i);
					}
					
					/* Key expansion */
					_key_exp(&c, &d, k2);
					
					/* One DES round */
					s = _ec_des_f(r, k2);
					
					if(desmode != HASH)
					{
						_key_rotate_ec(&c, &d, ROTATE_RIGHT, i);
					}
					
					/* Swap first two bytes if it's a hash routine */
					if(desmode == HASH)
					{
						s = ((s >> 8) & 0xFF0000L) | ((s << 8) & 0xFF000000L) | (s & 0x0000FFFFL);
					}
				}
				break;

			/* EC-S2 */
			case EC_S:
				{
					/* Key rotation */
					_key_rotate_ec(&c, &d, ROTATE_LEFT, i);
					
					/* Key expansion */
					_key_exp(&c, &d, k2);
					
					/* One DES round */
					s = _ec_des_f(r, k2);
				}
				break;

			/* EC-3DES */
			case EC_3DES:
				{
					/* Key rotation */
					if(rnd != 2)
					{
						_key_rotate_ec(&c, &d, ROTATE_LEFT, i);
					}
					
					/* Key expansion */
					_key_exp(&c, &d, k2);
					
					/* One DES round */
					s = _ec_des_f(r, k2);
					
					if(rnd == 2)
					{
						_key_rotate_ec(&c, &d, ROTATE_RIGHT, i);
					}
				}
				break;
		}
		
		/* Rotate halves around */
		r3 = l ^ s;
		l = r;
		r = r3;
	}
	
	/* Put everything together */
	for(i = 3; i >= 0; i--)
	{
		data[3 - i] = r >> (8 * i);
		data[7 - i] = l >> (8 * i);
	}
	
	/* Final permutation for Eurocrypt S2/3DES */
	if(emode != EC_M)
	{
		_permute_ec(data, _ipp, 64);
	}
}

static void _calc_ec_hash(uint8_t *hash, uint8_t *msg, int mode, int msglen, const uint8_t *key)
{
	int i, r;
	
	/* Iterate through data */
	for(i = 0; i < msglen; i++)
	{
		hash[(i % 8)] ^= msg[i];
		
		if(i % 8 == 7)
		{
			/* Three rounds for 3DES mode, one round for others */
			for(r = 0; r < (mode != EC_3DES ? 1 : 3); r++) 
			{
				/* Use second key on second round in 3DES */
				_eurocrypt(hash, key + (r != 1 ? 0 : 7), HASH, mode, r + 1);
			}
		}
	}
	
	/* Final interation - EC-M only */
	if(mode == EC_M)
	{
		_eurocrypt(hash, key, HASH, mode, 1);
	}
}

static void _build_ecm_hash_data(uint8_t *hash, const eurocrypt_t *e, int x)
{
	uint8_t msg[MAC_PAYLOAD_BYTES];
	int msglen;
	
	/* Clear hash memory */
	memset(hash, 0, 8);
	
	/* Build the hash message */
	msglen = 0;
	
	/* EC-S2 and EC-3DES */
	if(e->mode->emode != EC_M)
	{
		/* Copy PPID */
		memcpy(msg, e->ecm_pkt + 5, 3);
		msglen += 3;
		
		/* Third byte of PPUA contains key index, which needs to be masked for hashing */
		msg[2] &= 0xF0;
		
		/* Copy E1 04 data + 0xEA */
		memcpy(msg + msglen, e->ecm_pkt + (x - 24), 5);
		msglen += 5;
		
		/* Copy CWs */
		memcpy(msg + msglen, e->ecw[0], 8); msglen += 8;
		memcpy(msg + msglen, e->ecw[1], 8); msglen += 8;
	}
	else
	{
		/* EC-M */
		memcpy(msg, e->ecm_pkt + 8, x - 10);
		msglen += x - 10;
	}
	
	/* Calculate hash */
	_calc_ec_hash(hash, msg, e->mode->emode, msglen, e->mode->key);
}

static void _build_emmg_hash_data(uint8_t *hash, eurocrypt_t *e, int x)
{
	uint8_t msg[MAC_PAYLOAD_BYTES];
	int msglen;
	
	/* Clear hash memory */
	memset(hash, 0, 8);
	
	/* Build the hash data */
	msglen = 0;
	
	/* Copy entitlements into data buffer */
	memcpy(msg, e->emmg_pkt + 8, x); msglen += x - 10;
	_calc_ec_hash(hash, msg, e->mode->emode, msglen, e->emmode->key);
}

static void _build_emms_hash_data(uint8_t *hash, eurocrypt_t *e)
{
	uint8_t msg[MAC_PAYLOAD_BYTES];
	int msglen;
	
	/* Clear hash memory */
	memset(hash, 0, 8);
	
	/* Build the hash data */
	msglen = 0;
	
	if(e->emmode->emode == EC_M)
	{
		/* Copy card's Shared Address into hash buffer */
		hash[5] = e->emmode->sa[2];
		hash[6] = e->emmode->sa[1];
		hash[7] = e->emmode->sa[0];
		
		/* Do the initial hashing of the buffer */
		_eurocrypt(hash, e->emmode->key, HASH, e->mode->emode, 1);
		
		/* Copy ADF into data buffer */
		msg[msglen++] = 0x9e;
		msg[msglen++] = 0x20;
		memcpy(msg + msglen, e->emms_pkt + 6, 32); msglen += 32;
		
		/* Hash it */
		_calc_ec_hash(hash, msg, e->mode->emode, msglen, e->emmode->key);
		
		msglen = 0;
		
		/* Copy entitlements into data buffer */
		memcpy(msg + msglen, e->emmg_pkt + 8, 15); msglen += 15;
	}
	else
	{
		/* Copy ADF into data buffer */
		memcpy(msg + msglen, e->emms_pkt + 6, 35); msglen += 35;
		memset(msg + msglen, 0xFF, 5); msglen += 5;
	}
	
	/* Final hash */
	_calc_ec_hash(hash, msg, e->emmode->emode, msglen, e->emmode->key);
}

char *_get_sub_date(int b, const char *date)
{
	const int months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int d, m, y;
	char *dtm;
	
	dtm = malloc(sizeof(char) * 24);
	
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	
	m = tm.tm_mon + 1;
	y = tm.tm_year + 1900;
	
	if(strcmp(date, "TODAY") != 0)
	{
		sscanf(date, "%d/%d/%d", &d, &m, &y);
	}
	
	switch(b)
	{
		/* Today's day */
		case 0:
			d = tm.tm_mday;
			break;
			
		/* Last day in a month */
		case 31:
			d = months[m - 1];
			break;
		
		/* Default to passed value */
		default:
			d = b > 0 && b <= 31 ? b : 1;
			break;
	}
	
	sprintf(dtm, "\n%02d/%02d/%02d\n", d, m, y);
	
	return (dtm);
}

static void _encrypt_opkey(uint8_t *data, eurocrypt_t *e, int t)
{
	int r;
	uint8_t *emm = malloc(sizeof(e->mode->key) / sizeof(uint8_t));
	
	memset(emm, 0, 8);

	/* Pick the right key */
	if(e->mode->emode == EC_3DES)
	{
		memcpy(emm, e->mode->key + (t ? 7 : 0), 7);
	}
	else
	{
		memcpy(emm, e->mode->key, 8);
	}
	
	/* Do inverse permuted choice permutation for EC-S2/3DES keys */
	if(e->emmode->emode != EC_M) 
	{
		_permute_ec(emm, _ipc1, 64);
	}

	
	/* Three rounds for 3DES mode, one round for others */
	for(r = 0; r < (e->emmode->emode != EC_3DES ? 1 : 3); r++)
	{
		/* Use second key on second round in 3DES */
		_eurocrypt(emm, e->emmode->key + (r != 1 ? 0 : 7), ECM, e->emmode->emode, r + 1);
	}
	
	memcpy(data, emm, 8);
}

static void _encrypt_date(uint8_t *outdata, eurocrypt_t *e, uint8_t indata[8])
{
	int r;

	/* Three rounds for 3DES mode, one round for others */
	for(r = 0; r < (e->emmode->emode != EC_3DES ? 1 : 3); r++)
	{
		/* Use second key on second round in 3DES */
		_eurocrypt(indata, e->emmode->key + (r != 1 ? 0 : 7), ECM, e->emmode->emode, r + 1);
	}
	
	memcpy(outdata, indata, 8);
}


static uint8_t _update_ecm_packet(eurocrypt_t *e, int t, int m, char *ppv, int nd)
{
	int x;
	uint16_t b;
	uint8_t *pkt = e->ecm_pkt;
	
	memset(pkt, 0, MAC_PAYLOAD_BYTES * 2);
	
	/* PT - always 0x00 for ECM */
	x = 0;
	pkt[x++] = ECM;
	
	/* Command Identifier, CI */
	b  = (e->mode->cmode & 0x30) << 2; /* Crypto-algo type */
	b |= 1 << 1;                       /* Format bit - always 1 */
	b |= t << 0;                       /* Toggle bit */
	pkt[x++] = b;
	
	/* Command Length Indicator, CLI -- updated later */
	pkt[x++] = 0;
	
	/* PPID */
	pkt[x++] = 0x90; /* PI */
	pkt[x++] = 0x03; /* LI */
	memcpy(&pkt[x], e->mode->ppid, 3); x += 3;
	
	/* Undocumented meaning of this byte but appears to be padding */
	pkt[x++] = 0xDF; /* PI */
	pkt[x++] = 0x00; /* LI */
	
	if(e->mode->emode == EC_M)
	{
		/* CTRL */
		pkt[x++] = 0xE0;
		pkt[x++] = 0x01;
		b  = m ? 1 : 0 << 6;	/* Enable maturity rating */
		b = (nd ? 1 : 0) << 5;	/* Date verification */
		b |= m ? m : 0;			/* Maturity rating (4 bits) */
		pkt[x++] = b;
	}
	
	if(ppv != NULL)
	{
		uint32_t ppvi[2] = { 0, 0 };
		int i = 0;
		
		char tokens[0x7F];
		strcpy(tokens, ppv);
		
		char *ptr = strtok(tokens, ",");
		
		while(ptr != NULL)
		{
			ppvi[i++] = (uint32_t) atof(ptr);
			ptr = strtok(NULL, ",");
		}
		
		pkt[x++] = 0xE4;
		pkt[x++] = 0x05;
		pkt[x++] = (ppvi[0] >> 16) & 0xFF;
		pkt[x++] = (ppvi[0] >>  8) & 0xFF;
		pkt[x++] = (ppvi[0] >>  0) & 0xFF;
		pkt[x++] = ppvi[1] & 0xFF;
		pkt[x++] = 0x00;
	}
	else
	{
		/* CDATE + THEME/LEVEL */
		pkt[x++] = 0xE1; /* PI */
		pkt[x++] = 0x04; /* LI */
		uint16_t d = _get_ec_date(strcmp(e->mode->date, "TODAY") == 0 ? _get_sub_date(0, e->mode->date) : e->mode->date, e->mode->emode);
		pkt[x++] = (d & 0xFF00) >> 8;
		pkt[x++] = (d & 0x00FF) >> 0;
		memcpy(&pkt[x], e->mode->theme, 2); x += 2;
	}
	
	/* ECW/OCW */
	pkt[x++] = 0xEA; /* PI */
	pkt[x++] = 0x10; /* LI */
	memcpy(&pkt[x], e->ecw[0], 8); x += 8; /* ECW */
	memcpy(&pkt[x], e->ecw[1], 8); x += 8; /* OCW */
	
	/* HASH */
	pkt[x++] = 0xF0; /* PI */
	pkt[x++] = 0x08; /* LI */
	_build_ecm_hash_data(&pkt[x], e, x); x += 8;
	memcpy(e->ecm_hash, &pkt[x - 8], 8);
	
	/* Update the CI command length */
	pkt[2] = x - 3;
	
	return (x / ECM_PAYLOAD_BYTES);
}

static void _build_emmu_hash_data(uint8_t *hash, eurocrypt_t *e, int x)
{
	uint8_t msg[MAC_PAYLOAD_BYTES];
	int msglen;
	
	/* Clear hash memory */
	memset(hash, 0, 8);
	memset(msg, 0, MAC_PAYLOAD_BYTES);
	
	/* Build the hash data */
	msglen = 0;
	
	/* Copy provider ID into data buffer */
	memcpy(msg + msglen, e->emmode->ppid, 3); msglen += 3;
	
	/* Copy LABEL + 1 byte into data buffer */
	memcpy(msg + msglen, e->emmu_pkt + 15, 0x0C); msglen += 0x0C;
	
	/* Copy "what to do" byte */
	memcpy(msg + msglen, e->emmu_pkt + 40, 0x01); msglen += 0x01;
	
	/* Copy key */
	memcpy(msg + msglen, e->emmu_pkt + 28, 0x06); msglen += 0x06;
	memcpy(msg + msglen, e->emmu_pkt + 38, 0x02); msglen += 0x02;
	
	_calc_ec_hash(hash, msg, e->emmode->emode, msglen, e->emmode->key);
}

static uint8_t _update_emmu_packet(eurocrypt_t *e, int t)
{
	int i, x;
	uint16_t b;
	uint8_t *pkt = e->emmu_pkt;
	
	memset(pkt, 0, MAC_PAYLOAD_BYTES * 2);
	
	/* Packet Type */
	x = 0;
	pkt[x++] = 0;

	/* Unique Address - reversed */
	memcpy(&pkt[x], e->emmode->ua, 5); x += 5;
	
	/* Command Identifier, CI */
	b  = (e->emmode->cmode & 0x30) << 2; /* Crypto-algo type */
	b |= 1 << 1;                         /* Format bit - 0: fixed, 1: variable */
	b |= 1 << 0;                         /* 1 */
	pkt[x++] = b;
	
	/* Command Length Indicator, CLI -- updated later */
	pkt[x++] = 0;
	
	/* PPID */
	pkt[x++] = 0x90; /* PI */
	pkt[x++] = 0x03; /* LI */
	/* Provider ID and op-key to use */
	memcpy(&pkt[x], e->emmode->ppid, 3); x += 3;
	
	/* LABEL string - always included */
	pkt[x++] = 0xA7;
	pkt[x++] = 0x0B;
	memset(&pkt[x], 0x20, 0x0B);

	i = (int) (strchr(e->mode->channame, '(') - e->mode->channame);

	strncpy((char *) &pkt[x], e->mode->channame, i > 1 ? i - 1 : 0x0B);
	x += 0x0B;
		
	/* CLE - key */
	pkt[x++] = 0xEF;
	pkt[x++] = 0x06;

	if(++flag % 3 == 0)
	{
		uint8_t data[8];
		uint16_t d;

		/* Date/theme */
		d = _get_ec_date(_get_sub_date(1, e->mode->date), e->emmode->emode);
		data[0] = (d & 0xFF00) >> 8;
		data[1] = (d & 0x00FF) >> 0;
		d = _get_ec_date(_get_sub_date(31, e->mode->date), e->emmode->emode);
		data[2] = (d & 0xFF00) >> 8;
		data[3] = (d & 0x00FF) >> 0;

		/* Theme/level */
		memcpy(&data[4], e->mode->theme, 2);

		/* Padding */
		data[6] = 0x00;
		data[7] = 0x00;

		/* Date is encrypted in S2 mode */
		_encrypt_date(e->enc_data, e, data);
	}
	else
	{
		_encrypt_opkey(e->enc_data, e, t);
	}

	memcpy(&pkt[x], e->enc_data, 6); x += 6;	
	
	/* Padding */
	pkt[x++] = 0xDF;
	pkt[x++] = 0x00;
	
	/* EMM hash */
	pkt[x++] = 0xF0;
	pkt[x++] = 0x08;
	memcpy(&pkt[x], e->enc_data + 6, 2); x += 2;
	
	/* Action byte */
	/*
		01/02: dates (also writes label)
		10: first 8-bytes for label
		11: last 3 bytes for label
		2x: update key, where x is key number
		30: update issuer key 0
		31: update issuer key 1
		40: write "CA" byte in 10 02 CA >20<
		50: write prov-id
	*/

	/* ID to use and update */
	if(flag % 3 == 0)
	{
		b = 0x02; /* Update date */
	}
	else
	{
		b = 0x20; /* Update key */
	
		/* Index to update */
		if(e->emmode->emode == EC_3DES && e->emmode->cmode == EC_3DES)
		{
			b |= _tdesmap[(e->mode->ppid[2] & 0x0F) - 0x0C][t];
		}
		else
		{
			b |= e->mode->ppid[2] & 0x0F;
		}
	}

	pkt[x++] = b;

	_build_emmu_hash_data(&pkt[x], e, x);
	memcpy(e->emm_hash, &pkt[x], 8);
	memcpy(&pkt[x], e->emm_hash + 3, 5); x += 5;

	/* Update the CI command length */
	pkt[7] = x - 8;

	return(x / ECM_PAYLOAD_BYTES);
}

static void _update_emms_packet(eurocrypt_t *e, int t)
{
	int x;
	uint16_t b;
	uint8_t *pkt = e->emms_pkt;
	
	memset(pkt, 0, MAC_PAYLOAD_BYTES);
	
	x = 0;
	
	/* Packet Type */
	pkt[x++] = EMMS;
	
	/* Shared Address - reversed */
	memcpy(&pkt[x], e->emmode->sa, 3); x += 3;
	
	/* Command Identifier, CI */
	b  = (e->emmode->cmode & 0x30) << 2; /* Crypto-algo type */
	b |= 0 << 1;                         /* Format bit - 0: fixed, 1: variable */
	b |= 0 << 0;                         /* ADF - clear */
	pkt[x++] = b;
	
	/* Command Length Indicator, CLI */
	pkt[x++] = 0x28;
	
	/* ADF */
	memset(&pkt[x], 0xFF, 32); x += 32;
	
	if(e->emmode->emode == EC_M)
	{
		/* EMM hash */
		_build_emms_hash_data(&pkt[x], e); x += 8;
		memcpy(e->emm_hash, &pkt[x - 8], 8);
	}
	else
	{
		x -= 7;
		
		/* ID to use and update */
		if(++flag % 3 == 0)
		{
			b = 0x02; /* Update date */
		}
		else
		{
			b = 0x20; /* Update key */
		}
		
		/* Index to update */
		if(e->emmode->emode == EC_3DES && e->emmode->cmode == EC_3DES)
		{
			b |= _tdesmap[(e->mode->ppid[2] & 0x0F) - 0x0C][t];
		}
		else
		{
			b |= e->mode->ppid[2] & 0x0F;
		}
		
		pkt[x++] = b;
		
		/* Update key index */
		b  = (e->emmode->ppid[2] & 0x0F) << 4; 
		
		/* PPID to update */
		if(e->emmode->cmode == EC_M)
		{
			b |= (e->mode->ppid[1] & 0x0F);
		}
		else
		{
			b |= (e->mode->ppid[2] & 0xF0) >> 4;
		}
		
		pkt[x++] = b;
				
		if(flag % 3 == 0)
		{
			uint8_t data[8];
			uint16_t d;

			/* Date */
			d = _get_ec_date(_get_sub_date(1, e->mode->date), e->emmode->emode);
			data[0] = (d & 0xFF00) >> 8;
			data[1] = (d & 0x00FF) >> 0;
			d = _get_ec_date(_get_sub_date(31, e->mode->date), e->emmode->emode);
			data[2] = (d & 0xFF00) >> 8;
			data[3] = (d & 0x00FF) >> 0;

			/* Theme/level */
			memcpy(&data[4], e->mode->theme, 2);

			/* Padding */
			data[6] = 0x00;
			data[7] = 0x00;

			/* Date is encrypted in S2 mode */
			_encrypt_date(e->enc_data, e, data);
		}
		else
		{
			_encrypt_opkey(e->enc_data, e, t);
		}

		memcpy(&pkt[x], e->enc_data, 8); x += 8;
		
		/* EMM hash */
		_build_emms_hash_data(e->emm_hash, e);
		memcpy(&pkt[x], e->emm_hash + 3, 5);
	}
	
	mac_golay_encode(pkt + 1, 30);
}

/* Global EMM */
static uint8_t _update_emmg_packet(eurocrypt_t *e, int t, char *ppv)
{
	int x;
	uint16_t b, d;
	uint8_t *pkt = e->emmg_pkt;
	
	memset(pkt, 0, MAC_PAYLOAD_BYTES * 2);
	
	/* Packet Type */
	x = 0;
	pkt[x++] = EMMG;
	
	/* Command Identifier, CI */
	b  = (e->emmode->cmode & 0x30) << 2; /* Crypto-algo type */
	b |= 1 << 1;                         /* Format bit - 0: fixed, 1: variable */
	b |= t << 0;                         /* Toggle */
	pkt[x++] = b;
	
	/* Command Length Indicator, CLI -- updated later */
	pkt[x++] = 0;
	
	/* PPID */
	pkt[x++] = 0x90; /* PI */
	pkt[x++] = 0x03; /* LI */
	/* Provider ID and M-key to use for decryption of op-key */
	memcpy(&pkt[x], e->emmode->ppid, 3); x += 3;
	
	/* CTRL - Global EMM */
	pkt[x++] = 0xA0;
	pkt[x++] = 0x01;
	pkt[x++] = 0x00;
	
	if(ppv && t)
	{
		d = _get_ec_date(_get_sub_date(0, e->mode->date), e->mode->emode);
		pkt[x++] = 0xAB;
		pkt[x++] = 0x04;
		pkt[x++] = (d & 0xFF00) >> 8;
		pkt[x++] = (d & 0x00FF) >> 0;
		pkt[x++] = 0x0F;
		pkt[x++] = 0xFF;
	}
	else
	{
		/* Date/theme */
		pkt[x++] = 0xA8;
		pkt[x++] = 0x06;
		d = _get_ec_date(_get_sub_date(1, e->mode->date), e->emmode->emode);
		pkt[x++] = (d & 0xFF00) >> 8;
		pkt[x++] = (d & 0x00FF) >> 0;
		d = _get_ec_date(_get_sub_date(31, e->mode->date), e->emmode->emode);
		pkt[x++] = (d & 0xFF00) >> 8;
		pkt[x++] = (d & 0x00FF) >> 0;
		memcpy(&pkt[x], e->mode->theme, 2); x += 2;
		
		/* IDUP */
		pkt[x++] = 0xA1;
		pkt[x++] = 0x03;
		/* Provider ID and op-key to update */
		memcpy(&pkt[x], e->mode->ppid, 3); x += 3;
		
		/* Encrypted op-key */
		pkt[x++] = 0xEF;
		pkt[x++] = 0x08;
		_encrypt_opkey(e->enc_data, e, t);
		memcpy(&pkt[x], e->enc_data, 8); x += 8;
	}
	
	/* EMM hash */
	pkt[x++] = 0xF0;
	pkt[x++] = 0x08;
	_build_emmg_hash_data(&pkt[x], e, x); x += 8;
	
	memcpy(e->emm_hash, &pkt[x - 8], 8);
	
	/* Update the CI command length */
	pkt[2] = x - 3;
	
	return(x / ECM_PAYLOAD_BYTES);
}

static uint8_t _update_emmgs_packet(eurocrypt_t *e, int t)
{
	int x;
	uint16_t b;
	uint8_t *pkt = e->emmg_pkt;
	
	memset(pkt, 0, MAC_PAYLOAD_BYTES * 2);
	
	/* Packet Type */
	x = 0;
	pkt[x++] = EMMG;
	
	/* Command Identifier, CI */
	b  = (e->emmode->cmode & 0x30) << 2; /* Crypto-algo type */
	b |= 1 << 1;                         /* Format bit - 0: fixed, 1: variable */
	b |= t << 0;                         /* Toggle */
	pkt[x++] = b;
	
	/* Command Length Indicator, CLI -- updated later */
	pkt[x++] = 0;
	
	/* PPID */
	pkt[x++] = 0x90; /* PI */
	pkt[x++] = 0x03; /* LI */
	/* Provider ID and M-key to use for decryption of op-key */
	memcpy(&pkt[x], e->emmode->ppid, 3); x += 3;
	
	if(e->emmode->emode == EC_M)
	{
		/* IDUP */
		pkt[x++] = 0xA1;
		pkt[x++] = 0x03;
		/* Provider ID and op-key to update */
		memcpy(&pkt[x], e->mode->ppid, 3); x += 3;
		
		pkt[x++] = 0xEF;
		pkt[x++] = 0x08;
		_encrypt_opkey(e->enc_data, e, t);
		memcpy(&pkt[x], e->enc_data, 8); x += 8;
	}
	else
	{
		int i;

		/* LABEL string - always included */
		pkt[x++] = 0xA7;
		pkt[x++] = 0x0B;
		memset(&pkt[x], 0x20, 0x0B);

		i = (int) (strchr(e->mode->channame, '(') - e->mode->channame);

		strncpy((char *) &pkt[x], e->mode->channame, i > 1 ? i - 1 : 0x0B);
		x += 0x0B;

		/* Padding */
		pkt[x++] = 0xDF;
		pkt[x++] = 0x00;
	}
	
	/* Update the CI command length */
	pkt[2] = x - 3;
	
	return(x / ECM_PAYLOAD_BYTES);
}

static uint64_t _update_cw(eurocrypt_t *e, int t)
{
	uint64_t cw;
	int i, r;
	
	/* Fetch the next active CW */
	for(cw = i = 0; i < 8; i++)
	{
		cw = (cw << 8) | e->cw[t][i];
	}
	
	/* Generate a new CW */
	t ^= 1;
	
	for(i = 0; i < 8; i++)
	{
		e->cw[t][i] = e->ecw[t][i] = rand() & 0xFF;
	}
	
	/* Three rounds for 3DES mode, one round for others */
	for(r = 0; r < (e->mode->emode != EC_3DES ? 1 : 3); r++)
	{
		/* Use second key on second round in 3DES */
		_eurocrypt(e->ecw[t], e->mode->key + (r != 1 ? 0 : 7), ECM, e->mode->emode, r + 1);
	}
	
	return(cw);
}

void eurocrypt_next_frame(vid_t *vid, int frame)
{
	eurocrypt_t *e = &vid->mac.ec;
	
	/* Update the CW at the beginning of frames FCNT == 1 */
	if((frame & 0xFF) == 1)
	{
		int t = (frame >> 8) & 1;
		
		/* Fetch and update next CW */
		vid->mac.cw = _update_cw(e, t);
		
		/* Update the ECM packet */
		e->ecm_cont = _update_ecm_packet(e, t, vid->mac.ec_mat_rating, vid->conf.ec_ppv, vid->conf.nodate);
		
		/* Print ECM */
		if(vid->conf.showecm)
		{
			int i;
			if(frame == 1)
			{
				fprintf(stderr, "\n+----+----------------------+-------------------------+-------------------------+");
				fprintf(stderr, "----------------------------+----------------------------+");
				fprintf(stderr, "-------------------------+");
				fprintf(stderr, "\n| ## |   Operational Key    |   Encrypted CW (even)   |    Encrypted CW (odd)   |");
				fprintf(stderr, "    Decrypted CW (even)     |     Decrypted CW (odd)     |");
				fprintf(stderr, "           Hash          |");
			}
			fprintf(stderr, "\n+----+----------------------+-------------------------+-------------------------+");
			fprintf(stderr, "----------------------------+----------------------------+");
			fprintf(stderr, "-------------------------+");
			fprintf(stderr, "\n| %02X | ", e->mode->ppid[2] & 0x0F);
			for(i = 0; i < 7; i++) fprintf(stderr, "%02X ", e->mode->key[i]);
			fprintf(stderr, "| ");
			for(i = 0; i < 8; i++) fprintf(stderr, "%02X ", e->ecw[0][i]);
			fprintf(stderr, "| ");
			for(i = 0; i < 8; i++) fprintf(stderr, "%02X ", e->ecw[1][i]);
			fprintf(stderr, "| %s", t ? "  " : "->");
			for(i = 0; i < 8; i++) fprintf(stderr, "%02X ", e->cw[0][i]);
			fprintf(stderr, " | %s", t ? "->" : "  ");
			for(i = 0; i < 8; i++) fprintf(stderr, "%02X ", e->cw[1][i]);
			fprintf(stderr, " | ");
			for(i = 0; i < 8; i++) fprintf(stderr, "%02X ", (uint8_t) e->ecm_hash[i]);
			fprintf(stderr, "|");
		}
	}
	
	/* Send an ECM packet every 12 frames - ~0.5s */
	if(frame % 12 == 0)
	{
		uint8_t pkt[MAC_PAYLOAD_BYTES];
		memset(pkt, 0, MAC_PAYLOAD_BYTES);
		int i;
		
		/* Break up the ECM packet, if required */
		for(i = 0; i <= e->ecm_cont; i++)
		{
			memcpy(pkt, e->ecm_pkt + (i * ECM_PAYLOAD_BYTES), ECM_PAYLOAD_BYTES + 1);
			
			pkt[0] = ECM;
			
			/* Golay encode the payload */
			mac_golay_encode(pkt + 1, 30);
			mac_write_packet(vid, 0, e->ecm_addr, i, pkt, 0);
		}	
	}

	/* Send EMMs every ~10 seconds, if available */
	if(e->emmode->id != NULL)
	{
		if((vid->frame & 0xFF) == 0x7F)
		{
			/* Generate EMM-Global packet */
			if(e->emmode->emmtype == EMMG)
			{
				uint8_t pkt[MAC_PAYLOAD_BYTES];
				memset(pkt, 0, MAC_PAYLOAD_BYTES);
				
				int i;
				
				int t = (vid->frame >> 8) & 1;
				
				e->emm_cont = _update_emmg_packet(e, t, vid->conf.ec_ppv);
				
				/* Break up the EMM-G packet, if required */
				for(i = 0; i <= e->emm_cont; i++)
				{
					memcpy(pkt, e->emmg_pkt + (i * ECM_PAYLOAD_BYTES), ECM_PAYLOAD_BYTES + 1);
					
					pkt[0] = e->emmode->emmtype;
					
					/* Golay encode the payload */
					mac_golay_encode(pkt + 1, 30);
					
					/* Write the packet */
					mac_write_packet(vid, 0, e->emm_addr, i, pkt, 0);
				}
			}
			
			/* Generate EMM-Unique packet */
			if(e->emmode->emmtype == EMMU)
			{
				uint8_t pkt[MAC_PAYLOAD_BYTES];
				memset(pkt, 0, MAC_PAYLOAD_BYTES);
				
				int i;
				
				int t = (vid->frame >> 8) & 1;
				
				e->emm_cont = _update_emmu_packet(e, t);
				
				/* Break up the EMM-U packet, if required */
				for(i = 0; i <= e->emm_cont; i++)
				{
					memcpy(pkt, e->emmu_pkt + (i * ECM_PAYLOAD_BYTES), ECM_PAYLOAD_BYTES + 1);
					
					pkt[0] = EMMU;

					/* Golay encode the payload */
					mac_golay_encode(pkt + 1, 30);
					
					/* Write the packet */
					mac_write_packet(vid, 0, e->emm_addr, i, pkt, 0);
				}
			}
			
			/* Generate EMM-Shared packet */
			if(e->emmode->emmtype == EMMS)
			{
				uint8_t pkt[MAC_PAYLOAD_BYTES];
				memset(pkt, 0, MAC_PAYLOAD_BYTES);
				int i;
				
				int t = (vid->frame >> 8) & 1;
				
				/* Shared EMM packet requires EMM-Global packet before it */
				e->emm_cont = _update_emmgs_packet(e, t);
				
				/* Break up the EMM-G packet, if required */
				for(i = 0; i <= e->emm_cont; i++)
				{
					memcpy(pkt, e->emmg_pkt + (i * ECM_PAYLOAD_BYTES), ECM_PAYLOAD_BYTES + 1);
					
					pkt[0] = EMMG;
					
					/* Golay encode the payload */
					mac_golay_encode(pkt + 1, 30);
					
					mac_write_packet(vid, 0, e->emm_addr, i, pkt, 0);
				}
				
				/* Generate the EMM-S packet (always fixed length) */
				_update_emms_packet(e, t);
				
				mac_write_packet(vid, 0, e->emm_addr, 0, e->emms_pkt, 0);
			}
			
			/* Print EMM to console */
			if(vid->conf.showecm)
			{
				int i;
				fprintf(stderr, "\n\n ***** EMM *****");
				fprintf(stderr, "\nUnique address:\t\t");
				for(i = 0; i < 4; i++) fprintf(stderr, "%02X ", e->emmode->ua[3 - i]);
				fprintf(stderr, "\nShared address:\t\t");
				for(i = 0; i < 3; i++) fprintf(stderr, "%02X ", e->emmode->sa[2 - i]);
				fprintf(stderr, "\nManagement key   [%02X]:\t", e->emmode->ppid[2] & 0x0F);
				for(i = 0; i < 7; i++) fprintf(stderr, "%02X ", e->emmode->key[i]);
				fprintf(stderr, "\nDecrypted op key [%02X]:\t", e->mode->ppid[2] & 0x0F);
				for(i = 0; i < 7; i++) fprintf(stderr, "%02X ", e->mode->key[i]);
				fprintf(stderr, "\nEncrypted op key [%02X]:\t", e->mode->ppid[2] & 0x0F);
				for(i = 0; i < 8; i++) fprintf(stderr, "%02X ", e->enc_data[i]);
				fprintf(stderr,"\nHash:\t\t\t");
				for(i = 0; i < 8; i++) fprintf(stderr, "%02X ", (uint8_t) e->emm_hash[i]);
				fprintf(stderr,"\n");
			}
		}
	}
}

int eurocrypt_init(vid_t *vid, const char *mode)
{
	eurocrypt_t *e = &vid->mac.ec;
	
	memset(e, 0, sizeof(eurocrypt_t));
	
	/* Find the ECM mode */
	for(e->mode = _ec_modes; e->mode->id != NULL; e->mode++)
	{
		if(strcmp(mode, e->mode->id) == 0) break;
	}
	
	if(e->mode->id == NULL)
	{
		fprintf(stderr, "Unrecognised Eurocrypt mode.\n");
		return(VID_ERROR);
	}
	
	/* Find the EMM mode */
	for(e->emmode = _em_modes; e->emmode->id != NULL; e->emmode++)
	{
		if(strcmp(mode, e->emmode->id) == 0) break;
	}
	
	if(e->emmode->id == NULL)
	{
		fprintf(stderr, "Cannot find a matching EMM mode.\n");
	}
	
	/* ECM/EMM address */
	e->ecm_addr = 346;
	e->emm_addr = 347;
	
	/* Generate initial even and odd encrypted CWs */
	_update_cw(e, 0);
	_update_cw(e, 1);
	
	/* Generate initial packet */
	e->ecm_cont = _update_ecm_packet(e, 0, vid->mac.ec_mat_rating, vid->conf.ec_ppv, vid->conf.nodate);
	
	return(VID_OK);
}
