/**************************************************************************/
/*  game_data.h                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class GameDataSource {
public:
	virtual Ref<FileAccess> get_file(const String &p_path) = 0;
	virtual ~GameDataSource() {}
};

class GameData {
	friend class DirAccessGameData;

	struct GameDataFile {
		GameDataSource *source = nullptr;
		uint64_t size = 0;
	};

	struct GameDataDirectory {
		HashSet<String> directories;
		HashSet<String> files;
	};

	HashMap<String, GameDataFile> files;
	HashMap<String, GameDataDirectory> directories;
	Vector<GameDataSource *> sources;

	static inline GameData *singleton = nullptr;

	bool _get_directory_contents(const String &p_path, Vector<String> &r_directories, Vector<String> &r_files) const;

public:
	static constexpr const char *SCHEME = "game_data://";

	static GameData *get_singleton() { return singleton; }
	static bool is_game_data_path(const String &p_path);
	static bool normalize_path(const String &p_path, String &r_path, bool p_require_file = false);

	void add_source(GameDataSource *p_source);
	bool add_path(const String &p_path, uint64_t p_size, GameDataSource *p_source, bool p_replace_files = false);

	bool has_path(const String &p_path) const;
	bool has_directory(const String &p_path) const;
	int64_t get_size(const String &p_path) const;
	Ref<FileAccess> try_open_path(const String &p_path) const;

	GameData();
	~GameData();
};

class FileAccessGameData : public FileAccess {
	GDSOFTCLASS(FileAccessGameData, FileAccess);

	Ref<FileAccess> file;
	String path;

	virtual Error open_internal(const String &p_path, int p_mode_flags) override;
	virtual uint64_t _get_modified_time(const String &p_file) override { return 0; }
	virtual uint64_t _get_access_time(const String &p_file) override { return 0; }
	virtual int64_t _get_size(const String &p_file) override;
	virtual BitField<FileAccess::UnixPermissionFlags> _get_unix_permissions(const String &p_file) override { return 0; }
	virtual Error _set_unix_permissions(const String &p_file, BitField<FileAccess::UnixPermissionFlags> p_permissions) override { return ERR_UNAVAILABLE; }
	virtual bool _get_hidden_attribute(const String &p_file) override { return false; }
	virtual Error _set_hidden_attribute(const String &p_file, bool p_hidden) override { return ERR_UNAVAILABLE; }
	virtual bool _get_read_only_attribute(const String &p_file) override { return true; }
	virtual Error _set_read_only_attribute(const String &p_file, bool p_ro) override { return ERR_UNAVAILABLE; }

public:
	virtual bool is_open() const override;
	virtual String get_path() const override { return path; }
	virtual String get_path_absolute() const override { return path; }
	virtual void seek(uint64_t p_position) override;
	virtual void seek_end(int64_t p_position = 0) override;
	virtual uint64_t get_position() const override;
	virtual uint64_t get_length() const override;
	virtual bool eof_reached() const override;
	virtual uint64_t get_buffer(uint8_t *p_dst, uint64_t p_length) const override;
	virtual void set_big_endian(bool p_big_endian) override;
	virtual Error get_error() const override;
	virtual Error resize(int64_t p_length) override { return ERR_UNAVAILABLE; }
	virtual void flush() override;
	virtual bool store_buffer(const uint8_t *p_src, uint64_t p_length) override { return false; }
	virtual bool file_exists(const String &p_name) override;
	virtual void close() override;
};

class DirAccessGameData : public DirAccess {
	GDSOFTCLASS(DirAccessGameData, DirAccess);

	String current_path;
	Vector<String> list_directories;
	Vector<String> list_files;
	int directory_index = 0;
	int file_index = 0;
	bool current_is_directory = false;

	bool _resolve_path(const String &p_path, String &r_path) const;

public:
	virtual Error list_dir_begin() override;
	virtual String get_next() override;
	virtual bool current_is_dir() const override { return current_is_directory; }
	virtual bool current_is_hidden() const override { return false; }
	virtual void list_dir_end() override;
	virtual int get_drive_count() override { return 0; }
	virtual String get_drive(int p_drive) override { return String(); }
	virtual Error change_dir(String p_dir) override;
	virtual String get_current_dir(bool p_include_drive = true) const override;
	virtual Error make_dir(String p_dir) override { return ERR_UNAVAILABLE; }
	virtual bool file_exists(String p_file) override;
	virtual bool dir_exists(String p_dir) override;
	virtual bool is_writable(String p_dir) override { return false; }
	virtual Error rename(String p_from, String p_to) override { return ERR_UNAVAILABLE; }
	virtual Error remove(String p_name) override { return ERR_UNAVAILABLE; }
	virtual uint64_t get_space_left() override { return 0; }
	virtual bool is_link(String p_file) override { return false; }
	virtual String read_link(String p_file) override { return p_file; }
	virtual Error create_link(String p_source, String p_target) override { return ERR_UNAVAILABLE; }
	virtual String get_filesystem_type() const override { return "game_data"; }
};