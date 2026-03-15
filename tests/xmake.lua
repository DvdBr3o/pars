set_group("tests")

add_requires("catch2")

pars_test = function(test)
	target("tests." .. test)
	add_deps("pars")
	set_languages("cxx20")
	add_files("src/" .. test .. ".cpp")
	add_headerfiles("src/**.hpp")
	add_includedirs("src")
	add_packages("catch2")
	set_encodings("utf-8")
end

pars_test("basic")
pars_test("basic_v2")
pars_test("arithmetic")
pars_test("markdown")

