// bench/payloads.hpp — shared payload generators for the memory benches.
//
// Varied, valid payloads (no single repeated record) for csv/toml/ini/xml/clike;
// JSON comes from the real corpus slices (a payload dir), else a record array.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace bench_payloads {

inline std::string read_file(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

inline std::string gen_json(std::size_t target) {
	const std::string rec = "{\"id\":123,\"name\":\"x \\\"y\\\"\",\"a\":[1,2,3],\"n\":{\"z\":null}}";
	std::string		  out = "[";
	while (out.size() < target) {
		if (out.size() > 1) out.push_back(',');
		out += rec;
	}
	out.push_back(']');
	return out;
}

inline std::string gen_csv(std::size_t target) {
	static const char* rows[] = {
		"alpha,beta,\"quoted, field\",\"has \"\"q\"\"\",42,true\n",
		"x,y,z,1,2,3\n",
		"\"a,b\",\"c\"\"d\",e,100,false\n",
		"long_field_name,another,third,4.5,-6,true\n",
		"1,2,3,4,5,6,7,8\n",
		"city,pop,lat,lon\n",
	};
	std::string   out;
	std::uint32_t st = 0x9e3779b9u;
	while (out.size() < target) {
		st = st * 1664525u + 1013904223u;
		out += rows[st % (sizeof(rows) / sizeof(rows[0]))];
	}
	return out;
}

inline std::string gen_toml(std::size_t target) {
	std::string out;
	for (std::size_t k = 0; out.size() < target; ++k)
		out += "[t" + std::to_string(k) + "]\nhost = \"h" + std::to_string(k % 97)
			   + "\"\nport = " + std::to_string(1000 + (k * 37) % 9000) + "\nname = 'n"
			   + std::to_string(k % 13) + "'\n# comment { } , =\narr = [" + std::to_string(k % 5)
			   + ", " + std::to_string(k % 7) + ", " + std::to_string(k % 11) + "]\n";
	return out;
}

inline std::string gen_ini(std::size_t target) {
	std::string out;
	for (std::size_t k = 0; out.size() < target; ++k)
		out += "[section" + std::to_string(k) + "]\nkey = value" + std::to_string(k)
			   + "\nnum = " + std::to_string(k * 3) + "\n; comment\nother = x" + std::to_string(k % 9)
			   + "\n";
	return out;
}

inline std::string gen_xml(std::size_t target) {
	std::string out;
	for (std::size_t k = 0; out.size() < target; ++k)
		out += "<item id=\"" + std::to_string(k) + "\" name=\"x &amp; y" + std::to_string(k % 11)
			   + "\"><name>alpha" + std::to_string(k % 7) + "</name></item>";
	return out;
}

inline std::string gen_clike(std::size_t target) {
	std::string out;
	for (std::size_t k = 0; out.size() < target; ++k)
		out += "int x" + std::to_string(k) + "; /* c { ; } */ y" + std::to_string(k % 13) + " = "
			   + std::to_string(k % 100) + "; // { ; }\n";
	return out;
}

}  // namespace bench_payloads
