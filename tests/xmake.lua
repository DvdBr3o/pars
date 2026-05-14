set_group("tests")

add_requires("catch2")

pars_test = function(test)
target("tests." .. test)
	add_deps("pars")
	set_enabled(has_config("build_examples"))
    set_default(false)
	set_languages("cxx20")
	add_files("src/" .. test .. ".cpp")
	add_headerfiles("src/**.hpp")
	add_includedirs("src")
	add_packages("catch2")
	set_encodings("utf-8")
end

pars_test("basic")
pars_test("arithmetic")
pars_test("markdown")
pars_test("luna")
pars_test("luna_v2")
