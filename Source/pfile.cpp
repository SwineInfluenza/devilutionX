#include "all.h"
#include "../3rdParty/Storm/Source/storm.h"
#include "../DiabloUI/diabloui.h"
#include "file_util.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "demomode.h"

DEVILUTION_BEGIN_NAMESPACE

#ifdef SPAWN
#define PASSWORD_SINGLE "adslhfb1"
#define PASSWORD_MULTI "lshbkfg1"
#else
#define PASSWORD_SINGLE "xrgyrkj1"
#define PASSWORD_MULTI "szqnlsk1"
#endif

static char hero_names[MAX_CHARACTERS][PLR_NAME_LEN];
BOOL gbValidSaveFile;

void pfile_write_hero()
{
	DWORD save_num;
	PkPlayerStruct pkplr;

	save_num = pfile_get_save_num_from_name(plr[myplr]._pName);
	if (pfile_open_archive(TRUE, save_num)) {
		PackPlayer(&pkplr, myplr, gbMaxPlayers == 1);
		pfile_encode_hero(&pkplr);
		pfile_flush(gbMaxPlayers == 1, save_num);
	}
}

DWORD pfile_get_save_num_from_name(const char *name)
{
	DWORD i;

	for (i = 0; i < MAX_CHARACTERS; i++) {
		if (!strcasecmp(hero_names[i], name))
			break;
	}

	return i;
}

void pfile_encode_hero(const PkPlayerStruct *pPack)
{
	BYTE *packed;
	DWORD packed_len;
	char password[16] = PASSWORD_SINGLE;

	if (gbMaxPlayers > 1)
		strcpy(password, PASSWORD_MULTI);

	packed_len = codec_get_encoded_len(sizeof(*pPack));
	packed = (BYTE *)DiabloAllocPtr(packed_len);
	memcpy(packed, pPack, sizeof(*pPack));
	codec_encode(packed, sizeof(*pPack), packed_len, password);
	mpqapi_write_file("hero", packed, packed_len);
	mem_free_dbg(packed);
}

BOOL pfile_open_archive(BOOL update, DWORD save_num)
{
	char FileName[MAX_PATH];

	pfile_get_save_path(FileName, sizeof(FileName), save_num);
	if (OpenMPQ(FileName, save_num))
		return TRUE;

	return FALSE;
}

void pfile_get_save_path(char *pszBuf, DWORD dwBufSize, DWORD save_num)
{
	char path[MAX_PATH];

#ifdef SPAWN
	const char *fmt = "%sshare_%d.sv";

	if (gbMaxPlayers <= 1)
		fmt = "%sspawn%d.sv";
#else
	const char *fmt = "%smulti_%d.sv";

	if (gbMaxPlayers <= 1)
		fmt = "%ssingle_%d.sv";
#endif

	GetPrefPath(path, MAX_PATH);
	snprintf(pszBuf, MAX_PATH, fmt, path, save_num);
}

void pfile_flush(BOOL is_single_player, DWORD save_num)
{
	char FileName[MAX_PATH];

	pfile_get_save_path(FileName, sizeof(FileName), save_num);
	mpqapi_flush_and_close(FileName, is_single_player, save_num);
}

BOOL pfile_create_player_description(char *dst, DWORD len)
{
	char desc[128];
	_uiheroinfo uihero;

	myplr = 0;
	pfile_read_player_from_save();
	game_2_ui_player(plr, &uihero, gbValidSaveFile);
	UiSetupPlayerInfo(gszHero, &uihero, GAME_ID);

	if (dst != NULL && len) {
		if (UiCreatePlayerDescription(&uihero, GAME_ID, desc) == 0)
			return FALSE;
		SStrCopy(dst, desc, len);
	}
	return TRUE;
}

BOOL pfile_rename_hero(const char *name_1, const char *name_2)
{
	int i;
	DWORD save_num;
	_uiheroinfo uihero;
	BOOL found = FALSE;

	if (pfile_get_save_num_from_name(name_2) == MAX_CHARACTERS) {
		for (i = 0; i != MAX_PLRS; i++) {
			if (!strcasecmp(name_1, plr[i]._pName)) {
				found = TRUE;
				break;
			}
		}
	}

	if (!found)
		return FALSE;
	save_num = pfile_get_save_num_from_name(name_1);
	if (save_num == MAX_CHARACTERS)
		return FALSE;

	SStrCopy(hero_names[save_num], name_2, PLR_NAME_LEN);
	SStrCopy(plr[i]._pName, name_2, PLR_NAME_LEN);
	if (!strcasecmp(gszHero, name_1))
		SStrCopy(gszHero, name_2, sizeof(gszHero));
	game_2_ui_player(plr, &uihero, gbValidSaveFile);
	UiSetupPlayerInfo(gszHero, &uihero, GAME_ID);
	pfile_write_hero();
	return TRUE;
}

void pfile_flush_W()
{
	pfile_flush(TRUE, pfile_get_save_num_from_name(plr[myplr]._pName));
}

void game_2_ui_player(const PlayerStruct *p, _uiheroinfo *heroinfo, BOOL bHasSaveFile)
{
	memset(heroinfo, 0, sizeof(*heroinfo));
	strncpy(heroinfo->name, p->_pName, sizeof(heroinfo->name) - 1);
	heroinfo->name[sizeof(heroinfo->name) - 1] = '\0';
	heroinfo->level = p->_pLevel;
	heroinfo->heroclass = game_2_ui_class(p);
	heroinfo->strength = p->_pStrength;
	heroinfo->magic = p->_pMagic;
	heroinfo->dexterity = p->_pDexterity;
	heroinfo->vitality = p->_pVitality;
	heroinfo->gold = p->_pGold;
	heroinfo->hassaved = bHasSaveFile;
	heroinfo->herorank = p->pDiabloKillLevel;
#ifdef SPAWN
	heroinfo->spawned = TRUE;
#else
	heroinfo->spawned = FALSE;
#endif
}

BYTE game_2_ui_class(const PlayerStruct *p)
{
	BYTE uiclass;
	if (p->_pClass == PC_WARRIOR)
		uiclass = UI_WARRIOR;
	else if (p->_pClass == PC_ROGUE)
		uiclass = UI_ROGUE;
	else
		uiclass = UI_SORCERER;

	return uiclass;
}

BOOL pfile_ui_set_hero_infos(BOOL(*ui_add_hero_info)(_uiheroinfo *))
{
	DWORD i, save_num;
	char FileName[MAX_PATH];
	char NewFileName[MAX_PATH];
	BOOL showFixedMsg;

	memset(hero_names, 0, sizeof(hero_names));

	showFixedMsg = TRUE;
	for (i = 0; i < MAX_CHARACTERS; i++) {
		PkPlayerStruct pkplr;
		HANDLE archive = pfile_open_save_archive(&showFixedMsg, i);
		if (archive) {
			if (pfile_read_hero(archive, &pkplr)) {
				_uiheroinfo uihero;
				strcpy(hero_names[i], pkplr.pName);
				UnPackPlayer(&pkplr, 0, FALSE);
				game_2_ui_player(plr, &uihero, pfile_archive_contains_game(archive, i));
				ui_add_hero_info(&uihero);
			}
			pfile_SFileCloseArchive(archive);
		}
	}

	return TRUE;
}

BOOL pfile_read_hero(HANDLE archive, PkPlayerStruct *pPack)
{
	HANDLE file;
	DWORD dwlen, nSize;
	BYTE *buf;

	if (!SFileOpenFileEx(archive, "hero", 0, &file)) {
		return FALSE;
	} else {
		BOOL ret = FALSE;
		char password[16] = PASSWORD_SINGLE;
		nSize = 16;

		if (gbMaxPlayers > 1)
			strcpy(password, PASSWORD_MULTI);

		dwlen = SFileGetFileSize(file, NULL);
		if (dwlen) {
			DWORD read;
			buf = DiabloAllocPtr(dwlen);
			if (SFileReadFile(file, buf, dwlen, &read, NULL)) {
				read = codec_decode(buf, dwlen, password);
				if (read == sizeof(*pPack)) {
					memcpy(pPack, buf, sizeof(*pPack));
					ret = TRUE;
				}
			}
			if (buf)
				mem_free_dbg(buf);
		}
		SFileCloseFile(file);
		return ret;
	}
}

/**
 * @param showFixedMsg Display a dialog if a save file was corrected (deprecated)
 */
HANDLE pfile_open_save_archive(BOOL *showFixedMsg, DWORD save_num)
{
	char SrcStr[MAX_PATH];
	HANDLE archive;

	pfile_get_save_path(SrcStr, sizeof(SrcStr), save_num);
	if (SFileOpenArchive(SrcStr, 0x7000, FS_PC, &archive))
		return archive;
	return NULL;
}

void pfile_SFileCloseArchive(HANDLE hsArchive)
{
	SFileCloseArchive(hsArchive);
}

BOOL pfile_archive_contains_game(HANDLE hsArchive, DWORD save_num)
{
	HANDLE file;

	if (gbMaxPlayers != 1)
		return FALSE;

	if (!SFileOpenFileEx(hsArchive, "game", 0, &file))
		return FALSE;

	SFileCloseFile(file);
	return TRUE;
}

BOOL pfile_ui_set_class_stats(unsigned int player_class_nr, _uidefaultstats *class_stats)
{
	int c;

	c = pfile_get_player_class(player_class_nr);
	class_stats->strength = StrengthTbl[c];
	class_stats->magic = MagicTbl[c];
	class_stats->dexterity = DexterityTbl[c];
	class_stats->vitality = VitalityTbl[c];
	return TRUE;
}

char pfile_get_player_class(unsigned int player_class_nr)
{
	char pc_class;

	if (player_class_nr == UI_WARRIOR)
		pc_class = PC_WARRIOR;
	else if (player_class_nr == UI_ROGUE)
		pc_class = PC_ROGUE;
	else
		pc_class = PC_SORCERER;
	return pc_class;
}

BOOL pfile_ui_save_create(_uiheroinfo *heroinfo)
{
	DWORD save_num;
	char cl;
	PkPlayerStruct pkplr;

	save_num = pfile_get_save_num_from_name(heroinfo->name);
	if (save_num == MAX_CHARACTERS) {
		for (save_num = 0; save_num < MAX_CHARACTERS; save_num++) {
			if (!hero_names[save_num][0])
				break;
		}
		if (save_num == MAX_CHARACTERS)
			return FALSE;
	}
	if (!pfile_open_archive(FALSE, save_num))
		return FALSE;
	mpqapi_remove_hash_entries(pfile_get_file_name);
	strncpy(hero_names[save_num], heroinfo->name, PLR_NAME_LEN);
	hero_names[save_num][PLR_NAME_LEN - 1] = '\0';
	cl = pfile_get_player_class(heroinfo->heroclass);
	CreatePlayer(0, cl);
	strncpy(plr[0]._pName, heroinfo->name, PLR_NAME_LEN);
	plr[0]._pName[PLR_NAME_LEN - 1] = '\0';
	PackPlayer(&pkplr, 0, TRUE);
	pfile_encode_hero(&pkplr);
	game_2_ui_player(&plr[0], heroinfo, FALSE);
	pfile_flush(TRUE, save_num);
	return TRUE;
}

BOOL pfile_get_file_name(DWORD lvl, char *dst)
{
	const char *fmt;

	if (gbMaxPlayers > 1) {
		if (lvl)
			return FALSE;
		fmt = "hero";
	} else {
		if (lvl < 17)
			fmt = "perml%02d";
		else if (lvl < 34) {
			lvl -= 17;
			fmt = "perms%02d";
		} else if (lvl == 34)
			fmt = "game";
		else if (lvl == 35)
			fmt = "hero";
		else
			return FALSE;
	}
	sprintf(dst, fmt, lvl);
	return TRUE;
}

BOOL pfile_delete_save(_uiheroinfo *hero_info)
{
	DWORD save_num;
	char FileName[MAX_PATH];

	save_num = pfile_get_save_num_from_name(hero_info->name);
	if (save_num < MAX_CHARACTERS) {
		hero_names[save_num][0] = '\0';
		pfile_get_save_path(FileName, sizeof(FileName), save_num);
		RemoveFile(FileName);
	}
	return TRUE;
}

void pfile_read_player_from_save()
{
	HANDLE archive;
	DWORD save_num;
	PkPlayerStruct pkplr;

	save_num = pfile_get_save_num_from_name(gszHero);
	archive = pfile_open_save_archive(NULL, save_num);
	if (archive == NULL)
		app_fatal("Unable to open archive");
	if (!pfile_read_hero(archive, &pkplr))
		app_fatal("Unable to load character");

	UnPackPlayer(&pkplr, myplr, FALSE);
	gbValidSaveFile = pfile_archive_contains_game(archive, save_num);
	pfile_SFileCloseArchive(archive);
}

void GetTempLevelNames(char *szTemp)
{
	// BUGFIX: function call has no purpose
	pfile_get_save_num_from_name(plr[myplr]._pName);
	if (setlevel)
		sprintf(szTemp, "temps%02d", setlvlnum);
	else
		sprintf(szTemp, "templ%02d", currlevel);
}

void GetPermLevelNames(char *szPerm)
{
	DWORD save_num;
	BOOL has_file;

	save_num = pfile_get_save_num_from_name(plr[myplr]._pName);
	GetTempLevelNames(szPerm);
	if (!pfile_open_archive(FALSE, save_num))
		app_fatal("Unable to read to save file archive");

	has_file = mpqapi_has_file(szPerm);
	pfile_flush(TRUE, save_num);
	if (!has_file) {
		if (setlevel)
			sprintf(szPerm, "perms%02d", setlvlnum);
		else
			sprintf(szPerm, "perml%02d", currlevel);
	}
}

void pfile_get_game_name(char *dst)
{
	// BUGFIX: function call with no purpose
	pfile_get_save_num_from_name(plr[myplr]._pName);
	strcpy(dst, "game");
}

void pfile_remove_temp_files()
{
	if (gbMaxPlayers <= 1) {
		DWORD save_num = pfile_get_save_num_from_name(plr[myplr]._pName);
		if (!pfile_open_archive(FALSE, save_num))
			app_fatal("Unable to write to save file archive");
		mpqapi_remove_hash_entries(GetTempSaveNames);
		pfile_flush(TRUE, save_num);
	}
}

BOOL GetTempSaveNames(DWORD dwIndex, char *szTemp)
{
	const char *fmt;

	if (dwIndex < 17)
		fmt = "templ%02d";
	else if (dwIndex < 34) {
		dwIndex -= 17;
		fmt = "temps%02d";
	} else
		return FALSE;

	sprintf(szTemp, fmt, dwIndex);
	return TRUE;
}

void pfile_rename_temp_to_perm()
{
	DWORD dwChar, dwIndex;
	BOOL bResult;
	char szTemp[MAX_PATH];
	char szPerm[MAX_PATH];

	dwChar = pfile_get_save_num_from_name(plr[myplr]._pName);
	assert(dwChar < MAX_CHARACTERS);
	assert(gbMaxPlayers == 1);
	if (!pfile_open_archive(FALSE, dwChar))
		app_fatal("Unable to write to save file archive");

	dwIndex = 0;
	while (GetTempSaveNames(dwIndex, szTemp)) {
		bResult = GetPermSaveNames(dwIndex, szPerm);
		assert(bResult);
		dwIndex++;
		if (mpqapi_has_file(szTemp)) {
			if (mpqapi_has_file(szPerm))
				mpqapi_remove_hash_entry(szPerm);
			mpqapi_rename(szTemp, szPerm);
		}
	}
	assert(! GetPermSaveNames(dwIndex,szPerm));
	pfile_flush(TRUE, dwChar);
}

BOOL GetPermSaveNames(DWORD dwIndex, char *szPerm)
{
	const char *fmt;

	if (dwIndex < 17)
		fmt = "perml%02d";
	else if (dwIndex < 34) {
		dwIndex -= 17;
		fmt = "perms%02d";
	} else
		return FALSE;

	sprintf(szPerm, fmt, dwIndex);
	return TRUE;
}

void pfile_write_save_file(const char *pszName, BYTE *pbData, DWORD dwLen, DWORD qwLen)
{
	DWORD save_num;
	char FileName[MAX_PATH];

	pfile_strcpy(FileName, pszName);
	save_num = pfile_get_save_num_from_name(plr[myplr]._pName);
	{
		char password[16] = PASSWORD_SINGLE;
		if (gbMaxPlayers > 1)
			strcpy(password, PASSWORD_MULTI);

		codec_encode(pbData, dwLen, qwLen, password);
	}
	if (!pfile_open_archive(FALSE, save_num))
		app_fatal("Unable to write so save file archive");
	mpqapi_write_file(FileName, pbData, qwLen);
	pfile_flush(TRUE, save_num);
}

void pfile_strcpy(char *dst, const char *src)
{
	strcpy(dst, src);
}

BYTE *pfile_read(const char *pszName, DWORD *pdwLen)
{
	DWORD save_num, nread;
	char FileName[MAX_PATH];
	HANDLE archive, save;
	BYTE *buf;

	pfile_strcpy(FileName, pszName);
	save_num = pfile_get_save_num_from_name(plr[myplr]._pName);
	archive = pfile_open_save_archive(NULL, save_num);
	if (archive == NULL)
		app_fatal("Unable to open save file archive");

	if (!SFileOpenFileEx(archive, FileName, 0, &save))
		app_fatal("Unable to open save file");

	*pdwLen = SFileGetFileSize(save, NULL);
	if (*pdwLen == 0)
		app_fatal("Invalid save file");

	buf = (BYTE *)DiabloAllocPtr(*pdwLen);
	if (!SFileReadFile(save, buf, *pdwLen, &nread, NULL))
		app_fatal("Unable to read save file");
	SFileCloseFile(save);
	pfile_SFileCloseArchive(archive);

	{
		char password[16] = PASSWORD_SINGLE;
		DWORD nSize = 16;

		if (gbMaxPlayers > 1)
			strcpy(password, PASSWORD_MULTI);

		*pdwLen = codec_decode(buf, *pdwLen, password);
		if (*pdwLen == 0) {
			app_fatal("Invalid save file");
		}
	}
	return buf;
}

void pfile_update(BOOL force_save)
{
	// BUGFIX: these tick values should be treated as unsigned to handle overflows correctly
	static int save_prev_tc;

	if (gbMaxPlayers != 1) {
		int tick = SDL_GetTicks();
		if (force_save || tick - save_prev_tc > 60000) {
			save_prev_tc = tick;
			pfile_write_hero();
		}
	}
}

/**
 * Demomode save file comparison utility
 *
 */
void pfile_get_save_file_name(char *pszName, DWORD dwBufSize)
{
#ifdef SPAWN
	const char *format = (gbMaxPlayers <= 1) ? "spawn_%d.sv" : "share_%d.sv";
#else
	const char *format = (gbMaxPlayers <= 1) ? "single_%d.sv" : "multi_%d.sv";
#endif
	snprintf(pszName, dwBufSize, format, pfile_get_save_num_from_name(plr[myplr]._pName));
}

struct EncoderBuffer {
	EncoderBuffer(std::unique_ptr<byte[]> dataBuff, uint32_t dataSize, uint32_t buffSize)
	    : size_ { dataSize }
	    , buffer_ { dataBuff.release() }
	{
		uint32_t finalSize = std::min(codec_get_encoded_len(size_), buffSize);
		codec_encode(buffer_.get(), size_, finalSize, (gbMaxPlayers <= 1 ? PASSWORD_SINGLE : PASSWORD_MULTI));
		size_ = finalSize;
	}

	const byte *get() const
	{
		return buffer_.get();
	}

	uint32_t size() const
	{
		return size_;
	}

private:
	uint32_t size_;
	std::unique_ptr<byte[]> buffer_;
};

struct DecoderBuffer {
	DecoderBuffer(std::unique_ptr<byte[]> dataBuff, uint32_t buffSize)
		: size_ { buffSize }
	    , buffer_ { dataBuff.release() }
	{
		uint32_t finalSize;
		finalSize = codec_decode(buffer_.get(), size_, (gbMaxPlayers <= 1 ? PASSWORD_SINGLE : PASSWORD_MULTI));
		size_ = finalSize;
	}

	const byte *get() const
	{
		return buffer_.get();
	}

	uint32_t size() const
	{
		return size_;
	}

private:
	uint32_t size_;
	std::unique_ptr<byte[]> buffer_;
};

auto MpqOpenArchive(const char *szMpqName)
{
	using unique_archive = std::unique_ptr<void, decltype(&SFileCloseArchive)>;

	HANDLE hArchive;
	if (SFileOpenArchive(szMpqName, 0x7000, FS_PC, &hArchive))
		return unique_archive { hArchive, &SFileCloseArchive };

	return unique_archive { nullptr, &SFileCloseArchive };
}

DecoderBuffer MpqReadFile(HANDLE hArchive, const char *szFileName)
{
	using unique_file = std::unique_ptr<void, decltype(&SFileCloseFile)>;

	auto MpqOpenRFile = [](HANDLE hArchive, const char *szFileName) {
		HANDLE hFile;
		// SFileOpenFileEx(hArchive, szFileName, SFILE_OPEN_FROM_MPQ, &hFile)
		if (SFileOpenFileEx(hArchive, szFileName, 0, &hFile))
			return unique_file { hFile, &SFileCloseFile };

		return unique_file { nullptr, &SFileCloseFile };
	};

	unique_file file = MpqOpenRFile(hArchive, szFileName);
	if (file.get() == nullptr)
		app_fatal("Unable to open the file");

	uint32_t fileSize = SFileGetFileSize(file.get(), nullptr);
	std::unique_ptr<byte[]> buffer { new byte[fileSize] };

	if (!SFileReadFile(file.get(), buffer.get(), fileSize, nullptr, nullptr))
		app_fatal("Unable to read the file");

	return { std::move(buffer), fileSize };
}

void MpqWriteFile(HANDLE hArchive, const char *szFileName, const EncoderBuffer &dataBuff)
{
	using unique_file = std::unique_ptr<void, decltype(&SFileFinishFile)>;

	auto MpqOpenWFile = [](HANDLE hArchive, const char *szFileName, DWORD dwFileSize) {
		HANDLE hFile;
		// SFileCreateFile(hArchive, szFileName, 0, dwFileSize, LANG_NEUTRAL, MPQ_FILE_IMPLODE | MPQ_FILE_REPLACEEXISTING, &hFile)
		if (SFileCreateFile(hArchive, szFileName, 0, dwFileSize, 0, 0x00000100 | 0x80000000, &hFile))
			return unique_file { hFile, &SFileFinishFile };

		return unique_file { nullptr, &SFileFinishFile };
	};

	unique_file file = MpqOpenWFile(hArchive, szFileName, dataBuff.size());
	if (file.get() == nullptr)
		app_fatal("Unable to open the file");

	// SFileWriteFile(file.get(), dataBuff.get(), dataBuff.size(), MPQ_COMPRESSION_PKWARE)
	if (!SFileWriteFile(file.get(), dataBuff.get(), dataBuff.size(), 0x08))
		app_fatal("Unable to write the file");
}

void CopySaveFile(uint32_t saveNum, const std::string &targetPath)
{
	char savePath[MAX_PATH];

	pfile_get_save_path(savePath, sizeof(savePath), saveNum);
	std::fstream saveStream { savePath, std::fstream::in | std::fstream::binary };
	if (!saveStream)
		return;

	std::fstream targetStream { targetPath, std::fstream::out | std::fstream::binary | std::fstream::trunc };
	if (!targetStream)
		return;

	targetStream << saveStream.rdbuf();
}

class MemoryBuffer : public std::basic_streambuf<char> {
public:
	MemoryBuffer(char *data, size_t byteCount)
	{
		setg(data, data, data + byteCount);
		setp(data, data + byteCount);
	}
};

struct CompareInfo {
	DecoderBuffer &data;
	size_t currentPosition;
	size_t size;
	bool isTownLevel;
	bool dataExists;
};

struct CompareCounter {
	int reference;
	int actual;
	int max()
	{
		return std::max(reference, actual);
	}
	void checkIfDataExists(int count, CompareInfo &compareInfoReference, CompareInfo &compareInfoActual)
	{
		if (reference == count)
			compareInfoReference.dataExists = false;
		if (actual == count)
			compareInfoActual.dataExists = false;
	}
};

inline bool string_ends_with(std::string const &value, std::string const &ending)
{
	if (ending.size() > value.size())
		return false;
	return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void CreateDetailDiffs(const std::string &prefix, const std::string &memoryMapFile, CompareInfo &compareInfoReference, CompareInfo &compareInfoActual, std::unordered_map<std::string, size_t> &foundDiffs)
{
	char basePath[MAX_PATH];
	GetBasePath(basePath, MAX_PATH);

	// NOTE: File /memory_map/level.txt needs an additional entry "M_DL 12544 8 dMissile" at the very end
	std::string memoryMapFileAssetName { std::string { basePath } + "/memory_map/" + memoryMapFile + ".txt" };

	SDL_RWops *handle = SDL_RWFromFile(memoryMapFileAssetName.c_str(), "r");
	if (handle == nullptr) {
		app_fatal("MemoryMapFile %s is missing", memoryMapFile.c_str());
		return;
	}

	size_t readBytes = SDL_RWsize(handle);
	std::unique_ptr<byte[]> memoryMapFileData { new byte[readBytes] };
	SDL_RWread(handle, memoryMapFileData.get(), readBytes, 1);

	MemoryBuffer buffer { reinterpret_cast<char *>(memoryMapFileData.get()), readBytes };
	std::istream reader(&buffer);

	std::unordered_map<std::string, CompareCounter> counter;

	auto getCounter = [&](const std::string &counterAsString) {
		auto it = counter.find(counterAsString);
		if (it != counter.end())
			return it->second;
		int countFromMapFile = std::stoi(counterAsString);
		return CompareCounter { countFromMapFile, countFromMapFile };
	};

	auto addDiff = [&](const std::string &diffKey) {
		auto it = foundDiffs.find(diffKey);
		if (it == foundDiffs.end()) {
			foundDiffs.insert_or_assign(diffKey, 1);
		} else {
			foundDiffs.insert_or_assign(diffKey, it->second + 1);
		}
	};

	auto compareBytes = [&](size_t countBytes) {
		if (compareInfoReference.dataExists && compareInfoReference.currentPosition + countBytes > compareInfoReference.size)
			app_fatal("Comparsion failed. Too less bytes in reference to compare. Location: %s", prefix.c_str());
		if (compareInfoActual.dataExists && compareInfoActual.currentPosition + countBytes > compareInfoActual.size)
			app_fatal("Comparsion failed. Too less bytes in actual to compare. Location: %s", prefix.c_str());
		bool result = true;
		if (compareInfoReference.dataExists && compareInfoActual.dataExists)
			result = memcmp(compareInfoReference.data.get() + compareInfoReference.currentPosition, compareInfoActual.data.get() + compareInfoActual.currentPosition, countBytes) == 0;
		if (compareInfoReference.dataExists)
			compareInfoReference.currentPosition += countBytes;
		if (compareInfoActual.dataExists)
			compareInfoActual.currentPosition += countBytes;
		return result;
	};

	auto read32BitInt = [&](CompareInfo &compareInfo, bool useLE) {
		int32_t value = 0;
		if (!compareInfo.dataExists)
			return value;
		if (compareInfo.currentPosition + sizeof(value) > compareInfo.size)
			app_fatal("read32BitInt failed. Too less bytes to read.");
		memcpy(&value, compareInfo.data.get() + compareInfo.currentPosition, sizeof(value));
		if (useLE)
			value = SDL_SwapLE32(value);
		else
			value = SDL_SwapBE32(value);
		return value;
	};

	std::string line;
	while (std::getline(reader, line)) {
		if (line.size() > 0 && line[line.size() - 1] == '\r')
			line.resize(line.size() - 1);
		if (line.size() == 0)
			continue;
		std::stringstream lineStream { line };
		std::string command;
		std::getline(lineStream, command, ' ');

		bool dataExistsReference = compareInfoReference.dataExists;
		bool dataExistsActual = compareInfoActual.dataExists;

		// Skip Hellfire-specific entries
		if (string_ends_with(command, "_HF"))
			continue;
		if (string_ends_with(command, "_DA"))
			command.resize(command.size() - 3);
		if (string_ends_with(command, "_DL")) {
			if (compareInfoReference.isTownLevel && compareInfoActual.isTownLevel)
				continue;
			if (compareInfoReference.isTownLevel)
				compareInfoReference.dataExists = false;
			if (compareInfoActual.isTownLevel)
				compareInfoActual.dataExists = false;
			command.resize(command.size() - 3);
		}
		if (command == "R" || command == "LT" || command == "LC" || command == "LC_LE") {
			std::string bitsAsString;
			std::getline(lineStream, bitsAsString, ' ');
			std::string comment;
			std::getline(lineStream, comment);

			size_t bytes = static_cast<size_t>(std::stoi(bitsAsString) / 8);

			if (command == "LT") {
				int32_t valueReference = read32BitInt(compareInfoReference, false);
				int32_t valueActual = read32BitInt(compareInfoActual, false);
				assert(sizeof(valueReference) == bytes);
				compareInfoReference.isTownLevel = valueReference == 0;
				compareInfoActual.isTownLevel = valueActual == 0;
			}
			if (command == "LC" || command == "LC_LE") {
				int32_t valueReference = read32BitInt(compareInfoReference, command == "LC_LE");
				int32_t valueActual = read32BitInt(compareInfoActual, command == "LC_LE");
				assert(sizeof(valueReference) == bytes);
				counter.insert_or_assign(comment, CompareCounter { valueReference, valueActual });
			}

			if (!compareBytes(bytes)) {
				std::string diffKey { prefix + "." + comment };
				addDiff(diffKey);
			}
		} else if (command == "M") {
			std::string countAsString;
			std::getline(lineStream, countAsString, ' ');
			std::string bitsAsString;
			std::getline(lineStream, bitsAsString, ' ');
			std::string comment;
			std::getline(lineStream, comment);

			CompareCounter count = getCounter(countAsString);
			size_t bytes = static_cast<size_t>(std::stoi(bitsAsString) / 8);
			for (int i = 0; i < count.max(); i++) {
				count.checkIfDataExists(i, compareInfoReference, compareInfoActual);
				if (!compareBytes(bytes)) {
					std::string diffKey { prefix + "." + comment };
					addDiff(diffKey);
				}
			}
		} else if (command == "C") {
			std::string countAsString;
			std::getline(lineStream, countAsString, ' ');
			std::string subMemoryMapFile;
			std::getline(lineStream, subMemoryMapFile, ' ');
			std::string comment;
			std::getline(lineStream, comment);

			CompareCounter count = getCounter(countAsString);
			subMemoryMapFile.erase(std::remove(subMemoryMapFile.begin(), subMemoryMapFile.end(), '\r'), subMemoryMapFile.end());
			for (int i = 0; i < count.max(); i++) {
				count.checkIfDataExists(i, compareInfoReference, compareInfoActual);
				std::string subPrefix { prefix + "." + comment };
				CreateDetailDiffs(subPrefix, subMemoryMapFile, compareInfoReference, compareInfoActual, foundDiffs);
			}
		}

		compareInfoReference.dataExists = dataExistsReference;
		compareInfoActual.dataExists = dataExistsActual;
	}
}

struct CompareTargets {
	std::string fileName;
	std::string memoryMapFileName;
	bool isTownLevel;
};

HeroCompareResult CompareSaves(const std::string &actualSavePath, const std::string &referenceSavePath, bool logDetails)
{
	std::vector<CompareTargets> possibleFileToCheck;
	possibleFileToCheck.push_back({ "hero", "hero", false });
	possibleFileToCheck.push_back({ "game", "game", false });

	char szPerm[MAX_PATH];
	for (int i = 0; GetPermSaveNames(i, szPerm); i++) {
		possibleFileToCheck.push_back({ szPerm, "level", i == 0 });
	}

	auto actualArchive = MpqOpenArchive(actualSavePath.c_str());
	if (actualArchive.get() == nullptr)
		app_fatal("Unable to open save file archive %s", actualSavePath.c_str());
	auto referenceArchive = MpqOpenArchive(referenceSavePath.c_str());
	if (referenceArchive.get() == nullptr)
		app_fatal("Unable to open save file archive %s", referenceSavePath.c_str());

	bool compareResult = true;
	std::string message;
	for (const auto &compareTarget : possibleFileToCheck) {
		auto fileDataActual = SFileHasFile(actualArchive.get(), compareTarget.fileName.c_str()) ? MpqReadFile(actualArchive.get(), compareTarget.fileName.c_str()) : DecoderBuffer { nullptr, 0 };
		uint32_t fileSizeActual = fileDataActual.size();
		auto fileDataReference = SFileHasFile(referenceArchive.get(), compareTarget.fileName.c_str()) ? MpqReadFile(referenceArchive.get(), compareTarget.fileName.c_str()) : DecoderBuffer { nullptr, 0 };
		uint32_t fileSizeReference = fileDataReference.size();

		if (fileDataActual.get() == nullptr && fileDataReference.get() == nullptr) {
			continue;
		}
		if (fileSizeActual == fileSizeReference && memcmp(fileDataReference.get(), fileDataActual.get(), fileSizeActual) == 0)
			continue;
		compareResult = false;
		if (!message.empty())
			message.append("\n");
		if (fileSizeActual != fileSizeReference)
			message += "file \"" + compareTarget.fileName + "\" is different size. Expected: " + std::to_string(fileSizeReference) + " Actual: " + std::to_string(fileSizeActual);
		else
			message += "file \"" + compareTarget.fileName + "\" has different content.";
		if (!logDetails)
			continue;
		std::unordered_map<std::string, size_t> foundDiffs;
		CompareInfo compareInfoReference { fileDataReference, 0, fileSizeReference, compareTarget.isTownLevel, fileSizeReference != 0 };
		CompareInfo compareInfoActual { fileDataActual, 0, fileSizeActual, compareTarget.isTownLevel, fileSizeActual != 0 };
		CreateDetailDiffs(compareTarget.fileName, compareTarget.memoryMapFileName, compareInfoReference, compareInfoActual, foundDiffs);
		if (compareInfoReference.currentPosition != fileSizeReference)
			app_fatal("Comparsion failed. Uncompared bytes in reference. File: %s", compareTarget.fileName.c_str());
		if (compareInfoActual.currentPosition != fileSizeActual)
			app_fatal("Comparsion failed. Uncompared bytes in actual. File: %s", compareTarget.fileName.c_str());
		for (auto entry : foundDiffs) {
			message += "\nDiff found in " + entry.first + " count: " + std::to_string(entry.second);
		}
	}
	return { compareResult ? HeroCompareResult::Same : HeroCompareResult::Difference, message };
}

void pfile_write_save(HANDLE hArchive)
{
	uint32_t gameSaveSize = codec_get_encoded_len(FILEBUFF);
	std::unique_ptr<byte[]> gameSave { new byte[gameSaveSize] };

	uint32_t gameDataSize;
	SaveGameData(gameSave.get(), &gameDataSize);

	EncoderBuffer gameData { std::move(gameSave), gameDataSize, gameSaveSize };
	MpqWriteFile(hArchive, "game", gameData);

	char szTemp[MAX_PATH], szPerm[MAX_PATH];

	for (auto index = 0; GetTempSaveNames(index, szTemp) && GetPermSaveNames(index, szPerm); index++) {
		if (SFileHasFile(hArchive, szTemp)) {
			if (SFileHasFile(hArchive, szPerm)) {
				SFileRemoveFile(hArchive, szPerm, 0);
			}
			SFileRenameFile(hArchive, szTemp, szPerm);
		}
	}

	uint32_t heroSaveSize = codec_get_encoded_len(sizeof(PkPlayerStruct));
	std::unique_ptr<byte[]> heroSave { new byte[heroSaveSize] };

	// HACK: Packed player structure is not supposed to have any specific alignment
	static_assert(alignof(PkPlayerStruct) == 1, "Unexpected alignment for PkPlayerStruct!");
	PackPlayer(reinterpret_cast<PkPlayerStruct *>(heroSave.get()), myplr, gbMaxPlayers == 1);

	EncoderBuffer heroData { std::move(heroSave), sizeof(PkPlayerStruct), heroSaveSize };
	MpqWriteFile(hArchive, "hero", heroData);

	SFileCompactArchive(hArchive, nullptr, 0);
}

void pfile_write_hero_demo(int demo)
{
	char savePath[MAX_PATH], saveFile[MAX_PATH];

	GetPrefPath(savePath, MAX_PATH);
	pfile_get_save_file_name(saveFile, MAX_PATH);
	std::string referenceSavePath { std::string { savePath } + "demo_" + std::to_string(demo) + "_reference_" + saveFile };

	CopySaveFile(pfile_get_save_num_from_name(plr[myplr]._pName), referenceSavePath);

	auto saveWriter = MpqOpenArchive(referenceSavePath.c_str());
	pfile_write_save(saveWriter.get());
}

HeroCompareResult pfile_compare_hero_demo(int demo, bool logDetails)
{
	char savePath[MAX_PATH], saveFile[MAX_PATH];

	GetPrefPath(savePath, MAX_PATH);
	pfile_get_save_file_name(saveFile, MAX_PATH);
	std::string referenceSavePath { std::string { savePath } + "demo_" + std::to_string(demo) + "_reference_" + saveFile };

	if (!FileExists(referenceSavePath.c_str()))
		return { HeroCompareResult::ReferenceNotFound, {} };

	std::string actualSavePath { std::string { savePath } + "demo_" + std::to_string(demo) + "_actual_" + saveFile };
	{
		CopySaveFile(pfile_get_save_num_from_name(plr[myplr]._pName), actualSavePath);

		auto saveWriter = MpqOpenArchive(actualSavePath.c_str());
		pfile_write_save(saveWriter.get());
	}

	return CompareSaves(actualSavePath, referenceSavePath, logDetails);
}

DEVILUTION_END_NAMESPACE
