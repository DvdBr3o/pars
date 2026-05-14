set_project("pars")
set_version("0.1.0")
set_policy("package.requires_lock", true)
if is_plat("windows") then
    set_policy("package.precompiled", false)
    set_policy("package.install_always", true)
end

add_rules("mode.debug", "mode.release")

includes("tests")

add_requires("range-v3")
add_requires("tl_expected")
add_requires("utfcpp")
add_requires("fmt")

target("pars")
    set_languages("cxx20")
    set_kind("static")

    add_packages("range-v3", {public = true})
    add_packages("tl_expected", {public = true})
    add_packages("utfcpp", {public = true})
    add_packages("fmt", {public = true})

    add_headerfiles("src/**.hpp", {public = true})
    add_files("src/**.cpp", {public = true})
    add_includedirs("src", {public = true})

    -- add_cxxflags("clang::-ftemplate-backtrace-limit=0", {public = true, force = true})
    -- add_cxxflags("clang::-fconstexpr-backtrace-limit=0", {public = true, force = true})
    -- add_cxxflags("clang::-fno-elide-type", {public = true, force = true})
    
