/**************************************************************************/
/*  test_file_access.cpp                                                  */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_file_access)

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_game_data.h"
#include "core/io/file_access_memory.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "tests/test_utils.h"

namespace TestFileAccess {

class TestGameDataSource : public GameDataSource {
	HashMap<String, Vector<uint8_t>> files;

public:
	int size_probes = 0;

	void add_file(const String &p_path, const String &p_contents) {
		CharString contents = p_contents.utf8();
		Vector<uint8_t> data;
		data.resize(contents.length());
		for (int i = 0; i < contents.length(); i++) {
			data.write[i] = contents[i];
		}
		files[p_path] = data;
	}

	virtual int64_t get_size(const String &p_path) override {
		size_probes++;
		const Vector<uint8_t> *data = files.getptr(p_path);
		return data ? data->size() : -1;
	}

	virtual Ref<FileAccess> get_file(const String &p_path) override {
		const Vector<uint8_t> *data = files.getptr(p_path);
		if (!data) {
			return Ref<FileAccess>();
		}

		Ref<FileAccessMemory> file(memnew(FileAccessMemory));
		if (file->open_custom(data->ptr(), data->size()) != OK) {
			return Ref<FileAccess>();
		}
		return file;
	}
};

// Opens its sibling from get_file, registers a child and re-enters GameData, which must not hold the registry lock.
class ReentrantGameDataSource : public GameDataSource {
	String own_path;
	String sibling_path;
	String child_path;

public:
	ReentrantGameDataSource(const String &p_own_path, const String &p_sibling_path, const String &p_child_path) :
			own_path(p_own_path), sibling_path(p_sibling_path), child_path(p_child_path) {}

	virtual int64_t get_size(const String &p_path) override {
		// Same size as the sibling contents.
		return p_path == own_path ? 5 : -1;
	}

	virtual Ref<FileAccess> get_file(const String &p_path) override {
		GameData *game_data = GameData::get_singleton();
		if (!game_data->has_path(sibling_path)) {
			return Ref<FileAccess>();
		}
		game_data->add_path(child_path, 5, this, true);
		return game_data->try_open_path(sibling_path);
	}
};

class TestGameDataResourceLoader : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(List<String> *p_extensions) const override {
		p_extensions->push_back("game_data_test");
	}

	virtual String get_resource_type(const String &p_path) const override {
		return "Resource";
	}

	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override {
		Error error = OK;
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &error);
		if (r_error) {
			*r_error = error;
		}
		if (file.is_null()) {
			return Ref<Resource>();
		}

		Ref<Resource> resource(memnew(Resource));
		resource->set_name(file->get_as_text());
		return resource;
	}
};

TEST_CASE("[FileAccess] game_data virtual filesystem") {
	GameData *game_data = GameData::get_singleton();
	REQUIRE(game_data != nullptr);

	const String root = "game_data://file_access_game_data";
	const String hello_path = root.path_join("hello.txt");
	const String nested_path = root.path_join("nested/item.txt");
	const String resource_path = root.path_join("resource.game_data_test");

	TestGameDataSource *base_source = memnew(TestGameDataSource);
	base_source->add_file(hello_path, "base");
	base_source->add_file(nested_path, "nested");
	base_source->add_file(resource_path, "loaded");
	game_data->add_source(base_source);

	CHECK(game_data->add_path(root.path_join("./hello.txt"), 4, base_source));
	CHECK(game_data->add_path(nested_path, 6, base_source));
	CHECK(game_data->add_path(resource_path, 6, base_source));
	CHECK(!game_data->add_path(hello_path, 4, base_source));

	TestGameDataSource *overlay_source = memnew(TestGameDataSource);
	overlay_source->add_file(hello_path, "overlay");
	game_data->add_source(overlay_source);
	CHECK(game_data->add_path(hello_path, 7, overlay_source, true));

	Ref<FileAccess> file = FileAccess::open(hello_path, FileAccess::READ);
	REQUIRE(file.is_valid());
	CHECK(file->get_as_utf8_string() == "overlay");
	CHECK(FileAccess::exists(hello_path));
	CHECK(!FileAccess::exists(root.path_join("missing.txt")));
	CHECK(FileAccess::get_size(hello_path) == 7);
	CHECK(FileAccess::get_modified_time(hello_path) == 0);
	CHECK(FileAccess::get_access_time(hello_path) == 0);
	CHECK(FileAccess::get_read_only_attribute(hello_path));
	CHECK(FileAccess::set_read_only_attribute(hello_path, false) == ERR_UNAVAILABLE);

	Error error = OK;
	CHECK(FileAccess::open(hello_path, FileAccess::WRITE, &error).is_null());
	CHECK(error == ERR_UNAVAILABLE);
	CHECK(FileAccess::open(root.path_join("missing.txt"), FileAccess::READ, &error).is_null());
	CHECK(error == ERR_FILE_NOT_FOUND);

	Ref<DirAccess> directory = DirAccess::open(root);
	REQUIRE(directory.is_valid());
	CHECK(directory->file_exists("hello.txt"));
	CHECK(directory->dir_exists("nested"));
	CHECK(directory->change_dir("nested") == OK);
	CHECK(directory->file_exists("item.txt"));
	CHECK(directory->change_dir("..") == OK);
	CHECK(directory->get_current_dir() == root);
	CHECK(directory->make_dir("new_directory") == ERR_UNAVAILABLE);

	Ref<TestGameDataResourceLoader> loader(memnew(TestGameDataResourceLoader));
	ResourceLoader::add_resource_format_loader(loader, true);
	Ref<Resource> resource = ResourceLoader::load(resource_path, "", ResourceLoader::CACHE_MODE_IGNORE, &error);
	ResourceLoader::remove_resource_format_loader(loader);
	REQUIRE(resource.is_valid());
	CHECK(error == OK);
	CHECK(resource->get_name() == "loaded");
}

TEST_CASE("[FileAccess] game_data lazy source resolution") {
	GameData *game_data = GameData::get_singleton();
	REQUIRE(game_data != nullptr);

	const String root = "game_data://file_access_game_data_lazy";
	const String lazy_path = root.path_join("lazy.txt");
	const String shadowed_path = root.path_join("shadowed.txt");
	const String second_path = root.path_join("second.txt");
	const String missing_path = root.path_join("missing.txt");
	const String reentrant_path = root.path_join("reentrant.txt");
	const String reentrant_child_path = root.path_join("reentrant_child.txt");

	TestGameDataSource *first_source = memnew(TestGameDataSource);
	first_source->add_file(lazy_path, "first");
	first_source->add_file(shadowed_path, "first");
	game_data->add_source(first_source);

	TestGameDataSource *second_source = memnew(TestGameDataSource);
	second_source->add_file(shadowed_path, "second");
	second_source->add_file(second_path, "second");
	game_data->add_source(second_source);

	auto list_names = [](DirAccess *p_directory) {
		Vector<String> names;
		CHECK(p_directory->list_dir_begin() == OK);
		for (String name = p_directory->get_next(); !name.is_empty(); name = p_directory->get_next()) {
			names.push_back(name);
		}
		p_directory->list_dir_end();
		return names;
	};

	// Nothing under the root is registered yet, so the directory does not exist.
	CHECK(!game_data->has_directory(root));

	// Paths never added with add_path resolve through the sources, probed in registration order.
	CHECK(game_data->get_size(lazy_path) == 5);
	CHECK(first_source->size_probes == 1);
	CHECK(game_data->has_directory(root));
	Ref<FileAccess> lazy_file = FileAccess::open(lazy_path, FileAccess::READ);
	REQUIRE(lazy_file.is_valid());
	CHECK(lazy_file->get_as_utf8_string() == "first");

	// The first registered source wins, so later sources are not consulted for paths the first one provides.
	const int second_probes_before_shadowed = second_source->size_probes;
	Ref<FileAccess> shadowed_file = FileAccess::open(shadowed_path, FileAccess::READ);
	REQUIRE(shadowed_file.is_valid());
	CHECK(shadowed_file->get_as_utf8_string() == "first");
	CHECK(second_source->size_probes == second_probes_before_shadowed);

	// Paths only a later source provides fall through to it.
	CHECK(game_data->get_size(second_path) == 6);
	CHECK(second_source->size_probes == second_probes_before_shadowed + 1);

	// Resolved paths and their directories are cached, so repeated lookups probe no source.
	const int first_probes_cached = first_source->size_probes;
	const int second_probes_cached = second_source->size_probes;
	CHECK(FileAccess::exists(lazy_path));
	CHECK(game_data->get_size(shadowed_path) == 5);
	CHECK(game_data->get_size(second_path) == 6);
	CHECK(first_source->size_probes == first_probes_cached);
	CHECK(second_source->size_probes == second_probes_cached);

	Ref<DirAccess> directory = DirAccess::open(root);
	REQUIRE(directory.is_valid());
	Vector<String> names = list_names(directory.ptr());
	CHECK(names.has("lazy.txt"));
	CHECK(names.has("shadowed.txt"));
	CHECK(names.has("second.txt"));

	// Explicit registration still overrides a lazily resolved path when it replaces it.
	CHECK(!game_data->add_path(shadowed_path, 6, second_source));
	CHECK(game_data->add_path(shadowed_path, 6, second_source, true));
	CHECK(game_data->get_size(shadowed_path) == 6);
	Ref<FileAccess> replaced_file = FileAccess::open(shadowed_path, FileAccess::READ);
	REQUIRE(replaced_file.is_valid());
	CHECK(replaced_file->get_as_utf8_string() == "second");

	// A missing path stays absent on every lookup and never appears in the directory.
	for (int attempt = 0; attempt < 2; attempt++) {
		Error error = OK;
		CHECK(FileAccess::open(missing_path, FileAccess::READ, &error).is_null());
		CHECK(error == ERR_FILE_NOT_FOUND);
		CHECK(!FileAccess::exists(missing_path));
		CHECK(game_data->get_size(missing_path) == -1);
		CHECK(!game_data->has_path(missing_path));
	}
	CHECK(!list_names(directory.ptr()).has("missing.txt"));

	// A source whose get_file calls back into GameData must not deadlock on the registry lock.
	game_data->add_source(memnew(ReentrantGameDataSource(reentrant_path, lazy_path, reentrant_child_path)));
	Ref<FileAccess> reentrant_file = FileAccess::open(reentrant_path, FileAccess::READ);
	REQUIRE(reentrant_file.is_valid());
	CHECK(reentrant_file->get_as_utf8_string() == "first");
	CHECK(FileAccess::exists(reentrant_child_path));
}

TEST_CASE("[FileAccess] game_data unsupported suffixes bypass lazy sources") {
	GameData *game_data = GameData::get_singleton();
	REQUIRE(game_data != nullptr);

	const String root = "game_data://file_access_game_data_suffixes";
	const String remap_path = root.path_join("texture.png.remap");
	const String import_path = root.path_join("texture.png.import");
	const String registered_path = root.path_join("registered.txt.remap");

	TestGameDataSource *source = memnew(TestGameDataSource);
	source->add_file(remap_path, "remap");
	source->add_file(import_path, "import");
	source->add_file(registered_path, "registered");
	game_data->add_source(source);

	// The source would report these present, proving the suffix filter is what hides them.
	CHECK(source->get_size(remap_path) == 5);
	CHECK(source->get_size(import_path) == 6);
	const int probes_before = source->size_probes;

	CHECK(!game_data->has_path(remap_path));
	CHECK(game_data->get_size(remap_path) == -1);
	CHECK(game_data->try_open_path(remap_path).is_null());
	CHECK(!game_data->has_path(import_path));
	CHECK(game_data->get_size(import_path) == -1);
	CHECK(game_data->try_open_path(import_path).is_null());
	CHECK(source->size_probes == probes_before);

	// An explicitly registered path with a suffix is still honored and never probes the source.
	CHECK(game_data->add_path(registered_path, 10, source));
	CHECK(game_data->has_path(registered_path));
	CHECK(game_data->get_size(registered_path) == 10);
	Ref<FileAccess> registered_file = game_data->try_open_path(registered_path);
	REQUIRE(registered_file.is_valid());
	CHECK(registered_file->get_as_utf8_string() == "registered");
	CHECK(source->size_probes == probes_before);
}

TEST_CASE("[FileAccess] CSV read") {
	Ref<FileAccess> f = FileAccess::open(TestUtils::get_data_path("testdata.csv"), FileAccess::READ);
	REQUIRE(f.is_valid());

	Vector<String> header = f->get_csv_line(); // Default delimiter: ",".
	REQUIRE(header.size() == 4);

	Vector<String> row1 = f->get_csv_line(","); // Explicit delimiter, should be the same.
	REQUIRE(row1.size() == 4);
	CHECK(row1[0] == "GOOD_MORNING");
	CHECK(row1[1] == "Good Morning");
	CHECK(row1[2] == "Guten Morgen");
	CHECK(row1[3] == "Bonjour");

	Vector<String> row2 = f->get_csv_line();
	REQUIRE(row2.size() == 4);
	CHECK(row2[0] == "GOOD_EVENING");
	CHECK(row2[1] == "Good Evening");
	CHECK(row2[2].is_empty()); // Use case: not yet translated!
	// https://github.com/godotengine/godot/issues/44269
	CHECK_MESSAGE(row2[2] != "\"", "Should not parse empty string as a single double quote.");
	CHECK(row2[3] == "\"\""); // Intentionally testing only escaped double quotes.

	Vector<String> row3 = f->get_csv_line();
	REQUIRE(row3.size() == 6);
	CHECK(row3[0] == "Without quotes");
	CHECK(row3[1] == "With, comma");
	CHECK(row3[2] == "With \"inner\" quotes");
	CHECK(row3[3] == "With \"inner\", quotes\",\" and comma");
	CHECK(row3[4] == "With \"inner\nsplit\" quotes and\nline breaks");
	CHECK(row3[5] == "With \\nnewline chars"); // Escaped, not an actual newline.

	Vector<String> row4 = f->get_csv_line("~"); // Custom delimiter, makes inline commas easier.
	REQUIRE(row4.size() == 3);
	CHECK(row4[0] == "Some other");
	CHECK(row4[1] == "delimiter");
	CHECK(row4[2] == "should still work, shouldn't it?");

	Vector<String> row5 = f->get_csv_line("\t"); // Tab separated variables.
	REQUIRE(row5.size() == 3);
	CHECK(row5[0] == "What about");
	CHECK(row5[1] == "tab separated");
	CHECK(row5[2] == "lines, good?");
}

TEST_CASE("[FileAccess] Get as UTF-8 String") {
	SUBCASE("Newline == \\n (Unix)") {
		Ref<FileAccess> f_lf = FileAccess::open(TestUtils::get_data_path("line_endings_lf.test.txt"), FileAccess::READ);
		REQUIRE(f_lf.is_valid());
		String s_lf = f_lf->get_as_utf8_string();
		CHECK(s_lf == "Hello darkness\nMy old friend\nI've come to talk\nWith you again\n");
		f_lf->seek(0);
		CHECK(f_lf->get_line() == "Hello darkness");
		CHECK(f_lf->get_line() == "My old friend");
		CHECK(f_lf->get_line() == "I've come to talk");
		CHECK(f_lf->get_line() == "With you again");
		CHECK(f_lf->get_error() == Error::OK);
	}

	SUBCASE("Newline == \\r\\n (Windows)") {
		Ref<FileAccess> f_crlf = FileAccess::open(TestUtils::get_data_path("line_endings_crlf.test.txt"), FileAccess::READ);
		REQUIRE(f_crlf.is_valid());
		String s_crlf = f_crlf->get_as_utf8_string();
		CHECK(s_crlf == "Hello darkness\r\nMy old friend\r\nI've come to talk\r\nWith you again\r\n");
		f_crlf->seek(0);
		CHECK(f_crlf->get_line() == "Hello darkness");
		CHECK(f_crlf->get_line() == "My old friend");
		CHECK(f_crlf->get_line() == "I've come to talk");
		CHECK(f_crlf->get_line() == "With you again");
		CHECK(f_crlf->get_error() == Error::OK);
	}

	SUBCASE("Newline == \\r (Legacy macOS)") {
		Ref<FileAccess> f_cr = FileAccess::open(TestUtils::get_data_path("line_endings_cr.test.txt"), FileAccess::READ);
		REQUIRE(f_cr.is_valid());
		String s_cr = f_cr->get_as_utf8_string();
		CHECK(s_cr == "Hello darkness\rMy old friend\rI've come to talk\rWith you again\r");
		f_cr->seek(0);
		CHECK(f_cr->get_line() == "Hello darkness");
		CHECK(f_cr->get_line() == "My old friend");
		CHECK(f_cr->get_line() == "I've come to talk");
		CHECK(f_cr->get_line() == "With you again");
		CHECK(f_cr->get_error() == Error::OK);
	}

	SUBCASE("Newline == Mixed") {
		Ref<FileAccess> f_mix = FileAccess::open(TestUtils::get_data_path("line_endings_mixed.test.txt"), FileAccess::READ);
		REQUIRE(f_mix.is_valid());
		String s_mix = f_mix->get_as_utf8_string();
		CHECK(s_mix == "Hello darkness\nMy old friend\r\nI've come to talk\rWith you again");
		f_mix->seek(0);
		CHECK(f_mix->get_line() == "Hello darkness");
		CHECK(f_mix->get_line() == "My old friend");
		CHECK(f_mix->get_line() == "I've come to talk");
		CHECK(f_mix->get_line() == "With you again");
		CHECK(f_mix->get_error() == Error::ERR_FILE_EOF); // Not a bug; the file lacks a final newline.
	}
}

TEST_CASE("[FileAccess] Get/Store floating point values") {
	// BigEndian Hex: 0x40490E56
	// LittleEndian Hex: 0x560E4940
	float value = 3.1415f;

	SUBCASE("Little Endian") {
		const String file_path = TestUtils::get_data_path("floating_point_little_endian.bin");
		const String file_path_new = TestUtils::get_data_path("floating_point_little_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		CHECK_EQ(f->get_float(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->store_float(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}

	SUBCASE("Big Endian") {
		const String file_path = TestUtils::get_data_path("floating_point_big_endian.bin");
		const String file_path_new = TestUtils::get_data_path("floating_point_big_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		f->set_big_endian(true);
		CHECK_EQ(f->get_float(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->set_big_endian(true);
		fw->store_float(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}
}

TEST_CASE("[FileAccess] Get/Store floating point half precision values") {
	// IEEE 754 half-precision binary floating-point format:
	// sign exponent (5 bits)    fraction (10 bits)
	//  0        01101               0101010101
	// BigEndian Hex: 0x3555
	// LittleEndian Hex: 0x5535
	float value = 0.33325195f;

	SUBCASE("Little Endian") {
		const String file_path = TestUtils::get_data_path("half_precision_floating_point_little_endian.bin");
		const String file_path_new = TestUtils::get_data_path("half_precision_floating_point_little_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		CHECK_EQ(f->get_half(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->store_half(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}

	SUBCASE("Big Endian") {
		const String file_path = TestUtils::get_data_path("half_precision_floating_point_big_endian.bin");
		const String file_path_new = TestUtils::get_data_path("half_precision_floating_point_big_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		f->set_big_endian(true);
		CHECK_EQ(f->get_half(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->set_big_endian(true);
		fw->store_half(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}

	SUBCASE("4096 bytes fastlz compressed") {
		const String file_path = TestUtils::get_data_path("exactly_4096_bytes_fastlz.bin");

		Ref<FileAccess> f = FileAccess::open_compressed(file_path, FileAccess::READ, FileAccess::COMPRESSION_FASTLZ);
		const Vector<uint8_t> full_data = f->get_buffer(4096 * 2);
		CHECK(full_data.size() == 4096);
		CHECK(f->eof_reached());

		// Data should be empty.
		PackedByteArray reference;
		reference.resize_initialized(4096);
		CHECK(reference == full_data);

		f->seek(0);
		const Vector<uint8_t> partial_data = f->get_buffer(4095);
		CHECK(partial_data.size() == 4095);
		CHECK(!f->eof_reached());

		reference.resize_initialized(4095);
		CHECK(reference == partial_data);
	}
}

TEST_CASE("[FileAccess] Cursor positioning") {
	Ref<FileAccess> f = FileAccess::open(TestUtils::get_data_path("line_endings_lf.test.txt"), FileAccess::READ);
	REQUIRE(f.is_valid());

	String full = f->get_as_utf8_string();
	int64_t len = full.length();

	SUBCASE("Initial position is zero") {
		f->seek(0);
		CHECK(f->get_position() == 0);
	}

	SUBCASE("seek() moves cursor to absolute position") {
		f->seek(5);
		CHECK(f->get_position() == 5);
	}

	SUBCASE("seek() moves cursor beyond file size") {
		f->seek(len + 10);
		CHECK(f->get_position() == len + 10);
	}

	SUBCASE("seek_end() moves cursor to end of file") {
		f->seek_end(0);
		CHECK(f->get_position() == len);
	}

	SUBCASE("seek_end() with positive offset") {
		f->seek_end(1);
		CHECK(f->get_position() == len + 1);
	}

	SUBCASE("seek_end() with negative offset") {
		f->seek_end(-1);
		CHECK(f->get_position() == len - 1);

		char last_char = full[full.length() - 1];
		CHECK(f->get_8() == last_char);
	}

	SUBCASE("seek_end() beyond file size") {
		f->seek(5);
		f->seek_end(-len - 10); // seeking to a position below 0; ignored.
		CHECK(f->get_position() == 5);
	}
}

} // namespace TestFileAccess
