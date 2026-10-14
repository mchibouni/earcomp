-- Reads SoundSource's output effects through accessibility and writes them to a file:
-- one line per effect, "name | enabled | preset". Opens the SoundSource panel for about a
-- second and closes it again. Run with:
--   hs -c 'return dofile("<repo>/tools/soundsource-check.lua")("<out file>")'
local ax = hs.axuielement

return function(outPath)
  local lines = {}
  local function finish()
    local fh = io.open(outPath, "w")
    fh:write(table.concat(lines, "\n") .. "\n")
    fh:close()
  end
  local app = hs.application.applicationsForBundleID("com.rogueamoeba.soundsource")[1]
  if not app then lines = {"SoundSource is not running"} finish() return "done" end
  local axapp = ax.applicationElement(app)
  local bar = axapp:attributeValue("AXExtrasMenuBar")
  local item = bar and (bar:attributeValue("AXChildren") or {})[1]
  if not item then lines = {"SoundSource menu bar item not found"} finish() return "done" end

  local function find(el, pred, depth)
    depth = depth or 0
    if not el or depth > 18 then return nil end
    if pred(el) then return el end
    for _, c in ipairs(el:attributeValue("AXChildren") or {}) do
      local r = find(c, pred, depth + 1)
      if r then return r end
    end
  end

  item:performAction("AXPress")
  hs.timer.doAfter(1.0, function()
    local ok, err = pcall(function()
      local effects
      for _, w in ipairs(axapp:attributeValue("AXWindows") or {}) do
        effects = effects or find(w, function(e) return e:attributeValue("AXDescription") == "“Output”, Effects" end)
      end
      if not effects then table.insert(lines, "Output effects not found") return end
      for _, g in ipairs(effects:attributeValue("AXChildren") or {}) do
        local name = g:attributeValue("AXDescription") or "?"
        local enabled, preset = "?", ""
        for _, c in ipairs(g:attributeValue("AXChildren") or {}) do
          local d = c:attributeValue("AXDescription") or ""
          if c:attributeValue("AXRole") == "AXCheckBox" then enabled = tostring(c:attributeValue("AXValue")) end
          local p = d:match("^“(.*)”, popup button$")
          if p then preset = p end
        end
        table.insert(lines, name .. " | " .. enabled .. " | " .. preset)
      end
    end)
    if not ok then table.insert(lines, "ERROR: " .. tostring(err)) end
    pcall(function() item:performAction("AXPress") end)
    finish()
  end)
  return "started"
end
