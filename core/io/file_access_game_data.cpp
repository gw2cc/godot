/**************************************************************************/
/*  file_access_game_data.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "file_access_game_data.h"

#include "core/os/memory.h"

bool GameData::is_game_data_path(const String &p_path) {
	return p_path.begins_with(SCHEME);
}

bool GameData::normalize_path(const String &p_path, String &r_path, bool p_require_file) {
	if (!is_game_data_path(p_path)) {
		return false;
	}

	String path = p_path.trim_prefix(SCHEME).replace_char('\\', '/');
	if (path.begins_with("/")) {
		return false;
	}

	path = path.simplify_path();
	if (path == ".") {
		path.clear();
	}
	if (path == ".." || path.begins_with("../")) {
		return false;
	}

	r_path = path;
	return !p_require_file || !r_path.is_empty();
}

void GameData::add_source(GameDataSource *p_source) {
	ERR_FAIL_NULL(p_source);
	RWLockWrite lock(registry_lock);
	sources.push_back(p_source);
}

void GameData::_add_path_locked(const String &p_normalized_path, uint64_t p_size, GameDataSource *p_source) {
	files[p_normalized_path] = { p_source, p_size };
	String current_directory;
	directories[current_directory];
	Vector<String> path_parts = p_normalized_path.get_base_dir().split("/", false);
	for (const String &path_part : path_parts) {
		directories[current_directory].directories.insert(path_part);
		current_directory = current_directory.is_empty() ? path_part : current_directory.path_join(path_part);
		directories[current_directory];
	}

	directories[current_directory].files.insert(p_normalized_path.get_file());
}

bool GameData::add_path(const String &p_path, uint64_t p_size, GameDataSource *p_source, bool p_replace_files) {
	ERR_FAIL_NULL_V(p_source, false);

	String normalized_path;
	ERR_FAIL_COND_V(!normalize_path(p_path, normalized_path, true), false);

	RWLockWrite lock(registry_lock);
	if (files.has(normalized_path) && !p_replace_files) {
		return false;
	}

	_add_path_locked(normalized_path, p_size, p_source);
	return true;
}

bool GameData::_find_file(const String &p_normalized_path, GameDataFile &r_file) {
	Vector<GameDataSource *> source_snapshot;
	{
		RWLockRead lock(registry_lock);
		const GameDataFile *file = files.getptr(p_normalized_path);
		if (file) {
			r_file = *file;
			return true;
		}
		for (const char *suffix : UNSUPPORTED_SUFFIXES) {
			if (p_normalized_path.ends_with(suffix)) {
				return false;
			}
		}
		source_snapshot = sources;
	}

	const String full_path = String(SCHEME) + p_normalized_path;
	for (GameDataSource *source : source_snapshot) {
		const int64_t size = source->get_size(full_path);
		if (size < 0) {
			continue;
		}

		RWLockWrite lock(registry_lock);
		const GameDataFile *file = files.getptr(p_normalized_path);
		if (file) {
			r_file = *file;
			return true;
		}
		_add_path_locked(p_normalized_path, static_cast<uint64_t>(size), source);
		r_file = { source, static_cast<uint64_t>(size) };
		return true;
	}
	return false;
}

bool GameData::has_path(const String &p_path) {
	String normalized_path;
	if (!normalize_path(p_path, normalized_path, true)) {
		return false;
	}
	GameDataFile file;
	return _find_file(normalized_path, file);
}

bool GameData::has_directory(const String &p_path) const {
	String normalized_path;
	if (!normalize_path(p_path, normalized_path)) {
		return false;
	}
	RWLockRead lock(registry_lock);
	return directories.has(normalized_path);
}

int64_t GameData::get_size(const String &p_path) {
	String normalized_path;
	if (!normalize_path(p_path, normalized_path, true)) {
		return -1;
	}

	GameDataFile file;
	return _find_file(normalized_path, file) ? static_cast<int64_t>(file.size) : -1;
}

Ref<FileAccess> GameData::try_open_path(const String &p_path) {
	String normalized_path;
	if (!normalize_path(p_path, normalized_path, true)) {
		return Ref<FileAccess>();
	}

	GameDataFile file;
	if (!_find_file(normalized_path, file)) {
		return Ref<FileAccess>();
	}
	return file.source->get_file(String(SCHEME) + normalized_path);
}

bool GameData::_get_directory_contents(const String &p_path, Vector<String> &r_directories, Vector<String> &r_files) const {
	RWLockRead lock(registry_lock);
	const GameDataDirectory *directory = directories.getptr(p_path);
	if (!directory) {
		return false;
	}

	r_directories.clear();
	r_files.clear();
	for (const String &subdirectory : directory->directories) {
		r_directories.push_back(subdirectory);
	}
	for (const String &file : directory->files) {
		r_files.push_back(file);
	}
	return true;
}

GameData::GameData() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;
	directories[String()];
}

GameData::~GameData() {
	if (singleton == this) {
		singleton = nullptr;
	}

	for (GameDataSource *source : sources) {
		memdelete(source);
	}
}

Error FileAccessGameData::open_internal(const String &p_path, int p_mode_flags) {
	if (p_mode_flags & WRITE) {
		return ERR_UNAVAILABLE;
	}

	GameData *game_data = GameData::get_singleton();
	if (!game_data) {
		return ERR_UNAVAILABLE;
	}
	if (!game_data->has_path(p_path)) {
		return ERR_FILE_NOT_FOUND;
	}

	file = game_data->try_open_path(p_path);
	if (file.is_null()) {
		return ERR_FILE_CANT_OPEN;
	}

	path = p_path;
	big_endian = false;
	return OK;
}

int64_t FileAccessGameData::_get_size(const String &p_file) {
	GameData *game_data = GameData::get_singleton();
	return game_data ? game_data->get_size(p_file) : -1;
}

bool FileAccessGameData::is_open() const {
	return file.is_valid() && file->is_open();
}

void FileAccessGameData::seek(uint64_t p_position) {
	ERR_FAIL_COND(file.is_null());
	file->seek(p_position);
}

void FileAccessGameData::seek_end(int64_t p_position) {
	ERR_FAIL_COND(file.is_null());
	file->seek_end(p_position);
}

uint64_t FileAccessGameData::get_position() const {
	ERR_FAIL_COND_V(file.is_null(), 0);
	return file->get_position();
}

uint64_t FileAccessGameData::get_length() const {
	ERR_FAIL_COND_V(file.is_null(), 0);
	return file->get_length();
}

bool FileAccessGameData::eof_reached() const {
	return file.is_null() || file->eof_reached();
}

uint64_t FileAccessGameData::get_buffer(uint8_t *p_dst, uint64_t p_length) const {
	ERR_FAIL_COND_V(file.is_null(), 0);
	return file->get_buffer(p_dst, p_length);
}

void FileAccessGameData::set_big_endian(bool p_big_endian) {
	FileAccess::set_big_endian(p_big_endian);
	if (file.is_valid()) {
		file->set_big_endian(p_big_endian);
	}
}

Error FileAccessGameData::get_error() const {
	return file.is_valid() ? file->get_error() : ERR_FILE_CANT_OPEN;
}

void FileAccessGameData::flush() {
	if (file.is_valid()) {
		file->flush();
	}
}

bool FileAccessGameData::file_exists(const String &p_name) {
	GameData *game_data = GameData::get_singleton();
	return game_data && game_data->has_path(p_name);
}

void FileAccessGameData::close() {
	file.unref();
	path.clear();
}

bool DirAccessGameData::_resolve_path(const String &p_path, String &r_path) const {
	if (GameData::is_game_data_path(p_path)) {
		return GameData::normalize_path(p_path, r_path);
	}

	if (p_path == ".." && current_path.is_empty()) {
		r_path.clear();
		return true;
	}

	String path = current_path;
	if (!p_path.is_empty()) {
		path = path.is_empty() ? p_path : path.path_join(p_path);
	}
	return GameData::normalize_path(String(GameData::SCHEME) + path, r_path);
}

Error DirAccessGameData::list_dir_begin() {
	GameData *game_data = GameData::get_singleton();
	ERR_FAIL_NULL_V(game_data, ERR_UNAVAILABLE);
	ERR_FAIL_COND_V(!game_data->_get_directory_contents(current_path, list_directories, list_files), ERR_FILE_NOT_FOUND);

	directory_index = 0;
	file_index = 0;
	current_is_directory = false;
	return OK;
}

String DirAccessGameData::get_next() {
	if (directory_index < list_directories.size()) {
		current_is_directory = true;
		return list_directories[directory_index++];
	}
	if (file_index < list_files.size()) {
		current_is_directory = false;
		return list_files[file_index++];
	}

	return String();
}

void DirAccessGameData::list_dir_end() {
	list_directories.clear();
	list_files.clear();
	directory_index = 0;
	file_index = 0;
	current_is_directory = false;
}

Error DirAccessGameData::change_dir(String p_dir) {
	String path;
	ERR_FAIL_COND_V(!_resolve_path(p_dir, path), ERR_INVALID_PARAMETER);

	GameData *game_data = GameData::get_singleton();
	ERR_FAIL_NULL_V(game_data, ERR_UNAVAILABLE);
	ERR_FAIL_COND_V(!game_data->has_directory(String(GameData::SCHEME) + path), ERR_INVALID_PARAMETER);

	current_path = path;
	return OK;
}

String DirAccessGameData::get_current_dir(bool p_include_drive) const {
	return current_path.is_empty() ? String(GameData::SCHEME) : String(GameData::SCHEME) + current_path;
}

bool DirAccessGameData::file_exists(String p_file) {
	String path;
	GameData *game_data = GameData::get_singleton();
	return game_data && _resolve_path(p_file, path) && game_data->has_path(String(GameData::SCHEME) + path);
}

bool DirAccessGameData::dir_exists(String p_dir) {
	String path;
	GameData *game_data = GameData::get_singleton();
	return game_data && _resolve_path(p_dir, path) && game_data->has_directory(String(GameData::SCHEME) + path);
}
