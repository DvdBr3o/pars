set_project("pars")
add_rules("mode.debug", "mode.release", "mode.releasedbg")

-- Third-party baselines are pulled from xmake-repo (not vendored).  cuJSON and
-- simdcsv stay vendored because they are patched for benchmarking (deep source
-- edits), not pristine library usage.
add_requires("simdjson", "toml++", "pugixml", "inih")

-- CUDA 13.x ships CCCL/Thrust that requires MSVC's conforming preprocessor.
-- Apply to every CUDA target.  (simdjson cannot be mixed into an nvcc TU under
-- /Zc:preprocessor, so simdjson baselines are compiled as host files.)
rule("cccl_msvc")
    on_load(function (target)
        if is_plat("windows") then
            target:add("cuflags", "-Xcompiler=/Zc:preprocessor", {force = true})
        end
    end)

-- Target naming: pars.<category>.<name>[.<variant>]
--   pars                 the header-only library (root)
--   pars.test.*          correctness tests
--   pars.bench.*         benchmarks
--   pars.baseline.*      SOTA/vendored baselines
--   pars.paper.*         paper-data generation
-- The category lives in the first field so targets of a kind share a prefix.

target("pars")
    set_kind("headeronly")
    add_headerfiles("include/(pars/**.hpp)")
    add_includedirs("include", {public = true})

target("pars.test.model")
    set_kind("binary")
    add_deps("pars")
    add_files("tests/test_model.cpp")
    set_languages("cxxlatest")

-- Unified algebraic core: one prefix product, three summary instances.
target("pars.test.algebra")
    set_kind("binary")
    add_deps("pars")
    add_files("tests/test_algebra.cpp")
    set_languages("cxxlatest")

-- New unified pipeline: grammar -> core::plan -> host executor (prefix_reduce).
target("pars.test.core")
    set_kind("binary")
    add_deps("pars")
    add_files("tests/test_core.cpp")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- Meta backend (IR + rewrite): host-only static assertions.
target("pars.test.meta")
    set_kind("binary")
    add_deps("pars")
    add_files("tests/test_meta.cpp")
    set_languages("cxxlatest")

-- Meta backend GPU validation: rewritten pipeline + specialized kernels vs CPU.
target("pars.test.meta.gpu")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("tests/test_meta_gpu.cu")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- CPU scan/index throughput vs simdjson (JSON) and simdcsv (CSV).
target("pars.bench.scan")
    set_kind("binary")
    add_deps("pars")
    add_files("bench/bench_scan.cpp")
    set_languages("cxxlatest")
    add_cxxflags("-march=native")

-- Domain SOTA baselines: toml++, pugixml, inih, simdjson (from xmake-repo).
target("pars.bench.baselines")
    set_kind("binary")
    add_deps("pars")
    add_files("bench/bench_baselines.cpp")
    add_packages("simdjson", "toml++", "pugixml", "inih")
    set_languages("cxxlatest")
    add_cxxflags("-march=native")

-- Complete-parser workload on the user-defined hook (GPU stage-1 + user DOM).
target("pars.bench.parse")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/bench_parse.cu")
    add_files("bench/host_parse_simdjson.cpp")
    add_packages("simdjson")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- pars-side full parse for TOML/XML/INI (GPU R1-R6 + host post-R6 M+D).
target("pars.bench.parse.host")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/bench_parse_host.cu")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- Single-point query latency: pars (index navigation) vs SOTA (DOM lookup).
target("pars.bench.pointquery")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/bench_pointquery.cu")
    add_files("bench/host_pointquery.cpp")
    add_packages("simdjson", "toml++", "pugixml", "inih")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- cuJSON (GPU JSON) reference baseline, reproduced and patched for CUDA 13/CCCL.
-- Linux-only (uses x86intrin.h).
if not is_plat("windows") then
    target("pars.baseline.cujson")
        set_kind("binary")
        add_cugencodes("native")
        add_rules("cccl_msvc")
        add_files("third_party/cujson/reproduced/cuJSON-standardjson-total-parsing.cu")
        set_languages("cxxlatest")
        add_cuflags("-w", {force = true})
end

-- Host query tier (GPU stage-1 + lazy user iterator): Parsing / Query / Total.
target("pars.bench.query")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/bench_query.cu")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- Model-M CUDA backend: three-tier boundary benchmark.
target("pars.bench.boundary")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/bench_boundary.cu")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- Unified IR (core::plan) on the GPU: grammar -> core::plan -> scan kernels.
target("pars.bench.core")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/bench_core.cu")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- Two consumer variants: pars + lazy iterator / DOM builder vs simdjson
-- ondemand / dom (cuJSON is matched to the index/ondemand side).
target("pars.bench.variants")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/bench_variants.cu")
    add_files("bench/host_variants.cpp")
    add_packages("simdjson", "toml++", "pugixml", "inih")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- Tree schema (R7) on the GPU: associative reduction of integer leaves.
target("pars.bench.tree")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/bench_tree.cu")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- Isolation probe for the unified pars::scan<Grammar> API.
target("pars.probe.scan")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/probe_scan.cu")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- Device working-set measurement (memory footprint vs input size).
target("pars.bench.memory")
    set_kind("binary")
    add_deps("pars")
    add_cugencodes("native")
    add_rules("cccl_msvc")
    add_files("bench/bench_memory.cu")
    add_includedirs("bench")
    set_languages("cxxlatest")

-- Host-RAM footprint of the SOTA parsers (one language per process).
target("pars.bench.mem_sota")
    set_kind("binary")
    add_files("bench/bench_mem_sota.cpp")
    add_packages("simdjson", "toml++", "pugixml", "inih")
    add_includedirs("bench")
    set_languages("cxxlatest")
    add_cxxflags("-march=native")
-- CSV SOTA baseline (simdcsv): Linux-only (POSIX unistd.h / mmap).
if not is_plat("windows") then
    target("pars.baseline.simdcsv")
        set_kind("binary")
        add_files("third_party/simdcsv/main.cpp")
        add_files("third_party/simdcsv/io_util.cpp")
        add_includedirs("third_party/simdcsv")
        add_cxxflags("-march=native", "-include cstdint", {force = true})
        set_languages("cxxlatest")
end

-- Benchmark configuration for pars.paper.data (xmake f --scales=... --iters=...).
option("scales")
    set_default("8,16,32,64,128,256,512")
    set_description("Benchmark input scales in MB (comma separated)")
option("iters")
    set_default(3)
    set_description("Repetitions per measurement (for mean/p50/p95 distributions)")
option("dataset")
    set_default("")
    set_description("Optional real Standard JSON file (top-level array) used for the cuJSON/simdjson comparison")

-- C-like lexer baseline generated by flex (POSIX: the generated lexer uses
-- unistd.h), so Linux-only like simdcsv.
if not is_plat("windows") then
    target("pars.baseline.clike")
        set_kind("binary")
        add_deps("pars")
        add_files("bench/bench_flex_clike.cpp")
        add_files("bench/clike_lex.yy.cpp")
        set_languages("cxxlatest")
end

-- Category aggregators: the first field groups targets so a whole class builds
-- with one command (xmake build pars.test / pars.bench / pars.baseline).
target("pars.test")
    set_kind("phony")
    set_default(false)
    add_deps("pars.test.model", "pars.test.meta", "pars.test.meta.gpu", "pars.test.algebra", "pars.test.core")

target("pars.bench")
    set_kind("phony")
    set_default(false)
    add_deps("pars.bench.scan", "pars.bench.baselines", "pars.bench.parse",
             "pars.bench.parse.host", "pars.bench.pointquery", "pars.bench.query",
             "pars.bench.boundary")

target("pars.baseline")
    set_kind("phony")
    set_default(false)
    local baseline_deps = {}
    if not is_plat("windows") then
        table.insert(baseline_deps, "pars.baseline.cujson")
        table.insert(baseline_deps, "pars.baseline.simdcsv")
        table.insert(baseline_deps, "pars.baseline.clike")
    end
    add_deps(baseline_deps)

-- Deterministic paper-data generation: builds and runs the benchmarks over a
-- fixed set of scales/iterations and writes tidy CSVs for the figures.
-- Run with:  xmake build pars.paper.data
target("pars.paper.data")
    set_kind("phony")
    set_default(false)
    local paper_deps = {
        "pars.bench.boundary", "pars.bench.baselines",
        "pars.bench.parse", "pars.bench.query", "pars.bench.parse.host",
        "pars.bench.pointquery", "pars.bench.variants",
        "pars.bench.memory", "pars.bench.mem_sota",
    }
    if not is_plat("windows") then
        table.insert(paper_deps, "pars.baseline.simdcsv")
        table.insert(paper_deps, "pars.baseline.clike")
        table.insert(paper_deps, "pars.baseline.cujson")
    end
    add_deps(paper_deps)
    on_build(function (target)
        -- scales/iters come from xmake options (configure with
        --   xmake f --scales=16,64,256 --iters=10
        -- and rebuild).  Defaults are small so this target never hangs.
        local scales = {}
        for s in tostring(get_config("scales") or "16,64"):gmatch("[^,]+") do
            scales[#scales + 1] = tonumber(s)
        end
        local iters = tonumber(get_config("iters") or 3)
        local dataset = get_config("dataset") or ""
        -- If a real dataset is given, benchmark exactly that file (all formats
        -- and both GPU baselines see the same bytes).
        if dataset ~= "" then
            scales = {math.max(1, math.ceil(os.filesize(dataset) / (1024 * 1024)))}
        end
        local root = os.projectdir()
        local outdir = path.join(root, "paper", "plots", "data")
        os.mkdir(outdir)

        -- Resolve each dependency's actual binary (platform/arch independent).
        local function bin(name)
            return target:dep(name):targetfile()
        end
        local pG = bin("pars.bench.boundary")
        local bG = bin("pars.bench.baselines")
        local pP = bin("pars.bench.parse")
        local pQ = bin("pars.bench.query")
        local pPQ = bin("pars.bench.pointquery")
        local pH = bin("pars.bench.parse.host")
        local pV = bin("pars.bench.variants")
        local pM = bin("pars.bench.memory")
        local pMS = bin("pars.bench.mem_sota")
        local sd = (not is_plat("windows")) and bin("pars.baseline.simdcsv") or nil

        -- python3 on POSIX, python on Windows.
        local pyp = "python3"
        try {
            function () os.iorunv("python3", {"--version"}) end,
            catch { function () pyp = "python" end }
        }
        local header = "# format,impl,variant,scale_mb,GBps_mean,GBps_p50,GBps_p95,GBps_min,GBps_max,tokens,bytes_moved"

        local function rows_of(out)
            local t = {}
            for line in out:gmatch("[^\n]+") do
                if line:sub(1, 1) ~= "#" and #line > 0 then t[#t + 1] = line end
            end
            return t
        end

        local pr, br, sr, par, qr, vr = {}, {}, {}, {}, {}, {}
        for _, mb in ipairs(scales) do
            local po = os.iorunv(pG, {tostring(mb), tostring(iters)})
            for line in po:gmatch("[^\n]+") do
                if line:sub(1, 7) == "#stage," then
                    local f = line:match("^#stage,([^,]+),")
                    local rest = line:match("^#stage,[^,]+,(.*)$")
                    if f and rest then sr[#sr + 1] = f .. "," .. tostring(mb) .. "," .. rest end
                elseif line:sub(1, 1) ~= "#" and #line > 0 then
                    pr[#pr + 1] = line
                end
            end
            local bargs = {tostring(mb), tostring(iters)}
            if dataset ~= "" then
                table.insert(bargs, dataset)
            end
            for _, l in ipairs(rows_of(os.iorunv(bG, bargs))) do
                br[#br + 1] = l
            end
            -- complete-parser workload (GPU stage-1 + user hook): JSON
            for _, l in ipairs(rows_of(os.iorunv(pP, {tostring(mb), tostring(iters)}))) do
                par[#par + 1] = l
            end
            -- full parse for TOML/XML/INI (GPU R1-R6 + host post-R6 M+D)
            for _, l in ipairs(rows_of(os.iorunv(pH, {tostring(mb), tostring(iters)}))) do
                par[#par + 1] = l
            end
        end

        -- simdcsv on generated files (distribution over `iters` runs) — Linux only
        if sd then
            local tmpdir = path.join(root, "build", "paper_csv")
            os.mkdir(tmpdir)
            local rec = 'alpha,beta,"quoted, field","has ""q""",42,true\n'
            for _, mb in ipairs(scales) do
                local csvfile = path.join(tmpdir, "csv_" .. mb .. ".csv")
                local py = string.format(
                    "rec=%q\n" .. "t=%d*1024*1024\n" ..
                    "f=open(%q,'w')\n" ..
                    "while f.tell()<t: f.write(rec.replace('\\\\n','\\n'))\n" ..
                    "f.close()\n", rec, mb, csvfile)
                os.execv(pyp, {"-c", py})
                local vals = {}
                for _ = 1, iters do
                    local out = os.iorunv(sd, {"-i", "3", csvfile})
                    local v = out:match("GB/s:%s*([%d%.]+)")
                    if v then vals[#vals + 1] = tonumber(v) end
                end
                if #vals > 0 then
                    table.sort(vals)
                    local sum = 0
                    for _, x in ipairs(vals) do sum = sum + x end
                    local q = function(p) return vals[math.max(1, math.min(#vals, math.floor(p * (#vals - 1)) + 1))] end
                    br[#br + 1] = string.format("csv,simdcsv,native-ram,%d,%.3f,%.3f,%.3f,%.3f,%.3f,0,0",
                        mb, sum / #vals, q(0.50), q(0.95), vals[1], vals[#vals])
                end
            end
        end

        -- C-like SOTA: flex-generated lexer, over the scales (Linux only)
        local cf = (not is_plat("windows")) and bin("pars.baseline.clike") or nil
        if cf then
            for _, mb in ipairs(scales) do
                for _, l in ipairs(rows_of(os.iorunv(cf, {tostring(mb), tostring(iters)}))) do
                    br[#br + 1] = l
                end
            end
        end

        -- cuJSON (GPU JSON) reference: H2D + validation + Tokenize + Parser
        -- (their published TOTAL excludes D2H).  Variant encodes the interval.
        -- Linux only.
        local cj = (not is_plat("windows")) and bin("pars.baseline.cujson") or nil
        if cj then
            local jdir = path.join(root, "build", "paper_json")
            os.mkdir(jdir)
            -- cuJSON Figure 9 (Standard JSON): match scripts/run_cujson_fig9.sh —
            -- 10 fresh runs, average, parse the last number.  The same source file
            -- (src/reproduced/cuJSON-standardjson-total-parsing.cu) is used.
            local cujson_reps = 10
            for _, mb in ipairs(scales) do
                local jf
                if dataset ~= "" then
                    jf = dataset
                else
                    jf = path.join(jdir, "json_" .. mb .. ".json")
                    os.execv(
                        pyp, {path.join(root, "bench", "gen_json.py"), tostring(mb), jf}
                    )
                end
                local vals = {}
                for _ = 1, cujson_reps do
                    local out = os.iorunv(cj, {"-b", jf})
                    -- cuJSON default = its own Fig.9 interval (no D2H).
                    local nod = tonumber(out:match("CUJSON_NO_D2H%s*([%d%.]+)"))
                    if nod and nod > 0 then
                        vals[#vals + 1] = (mb * 1024.0 * 1024.0) / (nod / 1e3) / 1e9
                    end
                end
                if #vals > 0 then
                    table.sort(vals)
                    local sum = 0
                    for _, x in ipairs(vals) do sum = sum + x end
                    local q = function(p) return vals[math.max(1, math.min(#vals, math.floor(p * (#vals - 1)) + 1))] end
                    br[#br + 1] = string.format("json,cujson,R1-R6+T_in,%d,%.3f,%.3f,%.3f,%.3f,%.3f,0,0",
                        mb, sum / #vals, q(0.50), q(0.95), vals[1], vals[#vals])
                    -- cuJSON emits a structural index (no DOM) -> the ondemand side
                    vr[#vr + 1] = string.format("json,cujson,ondemand,%d,%.3f,0", mb, sum / #vals)
                end
            end
        end

        -- Two consumer variants (ondemand / dom) for every grammar.
        for _, mb in ipairs(scales) do
            for _, l in ipairs(rows_of(os.iorunv(pV, {tostring(mb), tostring(iters)}))) do
                vr[#vr + 1] = l
            end
        end

        -- Working-set figures.  pars: one process per (lang, placement) so the
        -- persistent per-grammar device cache cannot leak into the next
        -- measurement.  Host baselines: one process per (impl) for a clean RSS
        -- baseline.  Only the largest configured scale is measured (the ratio
        -- is what the figure reports; footprint is scale-invariant).
        local mem = {}
        local mem_scale = scales[#scales]
        local mem_langs = {"json", "csv", "toml", "ini", "xml", "clike"}
        for _, lang in ipairs(mem_langs) do
            for _, l in ipairs(rows_of(os.iorunv(pM, {lang, tostring(mem_scale)}))) do
                mem[#mem + 1] = l
            end
        end
        for _, lang in ipairs({"json", "toml", "xml", "ini"}) do
            for _, l in ipairs(rows_of(os.iorunv(pMS, {lang, tostring(mem_scale)}))) do
                mem[#mem + 1] = l
            end
        end
        -- cuJSON: peak device memory of its own Fig.9 harness, sampled from a
        -- child process (it has no built-in VRAM report).
        if (not is_plat("windows")) then
            local cj = bin("pars.baseline.cujson")
            local jdir = path.join(root, "build", "paper_json")
            os.mkdir(jdir)
            local jf = path.join(jdir, "json_" .. mem_scale .. ".json")
            if not os.isfile(jf) then
                os.execv(pyp, {path.join(root, "bench", "gen_json.py"), tostring(mem_scale), jf})
            end
            local ok, out = pcall(function ()
                return os.iorunv(pyp, {path.join(root, "bench", "measure_child_gpu.py"),
                                       "0", cj, "-b", jf})
            end)
            local used = ok and tonumber(out:match("(%d+)")) or nil
            if used then
                local input_mib = mem_scale
                mem[#mem + 1] = string.format("json,cujson,device,%d,%.1f,%.1f,%.2f",
                    mem_scale, input_mib, used, used / input_mib)
            end
        end

        io.writefile(path.join(outdir, "memory.csv"),
            "# format,impl,placement,scale_mb,input_mib,used_mib,ratio\n" ..
            table.concat(mem, "\n") .. "\n")
        io.writefile(path.join(outdir, "pars.csv"), header .. "\n" .. table.concat(pr, "\n") .. "\n")
        io.writefile(path.join(outdir, "baselines.csv"), header .. "\n" .. table.concat(br, "\n") .. "\n")
        io.writefile(path.join(outdir, "stages.csv"),
            "# format,scale_mb,r1_ms,r2r4_ms,r3r5_ms,r6_ms\n" .. table.concat(sr, "\n") .. "\n")
        io.writefile(path.join(outdir, "parse.csv"), header .. "\n" .. table.concat(par, "\n") .. "\n")
        io.writefile(path.join(outdir, "variants.csv"), header .. "\n" .. table.concat(vr, "\n") .. "\n")
        print("wrote paper/plots/data/{pars,baselines,stages,parse,variants,memory}.csv")
    end)
