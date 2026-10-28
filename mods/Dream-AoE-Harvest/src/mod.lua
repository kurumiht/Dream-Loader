-- ==============================================================================
-- Dream AoE Harvest (Lua Component)
-- Dedicated Farming Crop Gathering Mod (3.5m Radius, F8 Toggle)
-- Powered by dreamkurumiHT & Enshrouded Keen Holistic Engine v1.1.0
-- ==============================================================================

local TAG = "[DreamAoEHarvest]"
local VERSION = "1.1.0"
local HARVEST_UPDATE_MARKER_NS = 200000124

local defaults = {
    enabled = true,
    radius = 3.5,
    sameCropOnly = false,
    allowShiftVanilla = true,
    harvestWildBushes = true,
}

local function strip(text)
    return (tostring(text):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function toBoolean(text, fallback)
    local value = strip(text):lower()
    if value == "true" or value == "1" or value == "yes" or value == "on" then return true end
    if value == "false" or value == "0" or value == "no" or value == "off" then return false end
    return fallback
end

local function loadSettings()
    local result = {}
    for k, v in pairs(defaults) do result[k] = v end

    local configFile = nil
    if io.exists("config.ini") then
        configFile = "config.ini"
    elseif io.exists("config.txt") then
        configFile = "config.txt"
    end

    if not configFile then return result end

    local content = io.read_to_string(configFile)
    if not content then return result end

    for rawLine in content:gmatch("[^\r\n]+") do
        local line = strip(rawLine)
        if line ~= "" and not line:match("^[#;]") and not line:match("^%[") then
            local key, rawValue = line:match("^([%w_%-]+)%s*=%s*(.-)%s*$")
            if key and rawValue then
                local value = strip(rawValue:match("^[^#;]*") or rawValue)
                if key == "Enabled" then
                    result.enabled = toBoolean(value, result.enabled)
                elseif key == "Radius" or key == "radius" then
                    local num = tonumber(value)
                    if num then result.radius = num end
                elseif key == "SameCropOnly" or key == "sameCropOnly" then
                    result.sameCropOnly = toBoolean(value, result.sameCropOnly)
                elseif key == "AllowShiftVanilla" or key == "allowShiftVanilla" then
                    result.allowShiftVanilla = toBoolean(value, result.allowShiftVanilla)
                elseif key == "HarvestWildBushes" or key == "harvestWildBushes" then
                    result.harvestWildBushes = toBoolean(value, result.harvestWildBushes)
                end
            end
        end
    end

    return result
end

local cfg = loadSettings()
if not cfg.enabled then
    print(TAG .. " Disabled in configuration")
    return
end

cfg.radius = math.max(1.0, math.min(25.0, tonumber(cfg.radius) or 3.5))

local types = {
    pickup = "keen::ecs::PickupItem",
    destroy = "keen::ecs::DestroyOnLoot",
    inventory = "keen::ecs::Inventory",
    defaultInventory = "keen::ecs::DefaultInventory",
    zone = "keen::ecs::PickupItemZone",
    shape = "keen::ecs::TriggerShape",
    range = "keen::ecs::IsPlayerInRange",
    interaction = "keen::ecs::InteractionOffer",
    interactionRuntime = "keen::ecs::InteractionOfferRuntime",
    areaLoot = "keen::ecs::AreaLootOverride",
    lootAllMarker = "keen::ecs::DisableColliderModeChanges",
}

local function field(object, key)
    local ok, value = pcall(function() return object[key] end)
    if ok then return value end
    return nil
end

local function writeField(object, key, value)
    if not object then return false end
    return pcall(function() object[key] = value end)
end

local function componentIndex(components, wanted)
    for index, component in ipairs(components) do
        if field(component, "type") == wanted then
            return index
        end
    end
    return nil
end

local function has(components, wanted)
    return componentIndex(components, wanted) ~= nil
end

local function templateName(data)
    local debugName = field(data, "debugName")
    if debugName and tostring(debugName) ~= "" then return tostring(debugName) end
    local name = field(data, "name")
    if name then return tostring(name) end
    return ""
end

-- Mature Farm Crop vs Seedling Classification
local function classifyCrop(name, components)
    local lname = name:lower()

    -- Seedlings / Immature Crops: MUST NEVER BE HARVESTED EARLY!
    if lname:find("plant_growth") and not lname:find("_harvest") then
        return nil -- Protected seedling
    end

    -- Mature harvestable farm crops
    if lname:find("plant_growth") and lname:find("_harvest") then
        return "farm_crop"
    end

    -- Harvestable berry bushes and herbs
    if cfg.harvestWildBushes then
        if (lname:find("^scatter_loot_") and lname:find("_harvest")) or (lname:find("^plant_") and lname:find("grown")) then
            return "wild_plant"
        end
    end

    return nil
end

-- Tag crop for Area Looting / Proximity Harvest
local function enableCropAreaHarvest(components)
    local idx = componentIndex(components, types.areaLoot)
    if idx then
        local val = field(components[idx], "value")
        if val then writeField(val, "exclude", false) end
    else
        table.insert(components, { type = types.areaLoot, value = { exclude = false } })
    end

    if not has(components, types.lootAllMarker) then
        table.insert(components, { type = types.lootAllMarker, value = {} })
    end
end

local templates = game.assets.get_resources_by_type("keen::ecs::TemplateResource")
local stats = { matureCrops = 0, wildPlants = 0, protectedSeedlings = 0 }

for _, resource in ipairs(templates) do
    local data = field(resource, "data")
    local components = data and field(data, "components") or nil
    if components then
        local tName = templateName(data)
        local category = classifyCrop(tName, components)
        if category == "farm_crop" then
            enableCropAreaHarvest(components)
            stats.matureCrops = stats.matureCrops + 1
        elseif category == "wild_plant" then
            enableCropAreaHarvest(components)
            stats.wildPlants = stats.wildPlants + 1
        elseif tName:lower():find("plant_growth") then
            stats.protectedSeedlings = stats.protectedSeedlings + 1
        end
    end
end

print(string.format(
    "%s v%s Active! Radius=%.1fm (%d mature farm crops, %d wild plants armed, %d seedlings protected)",
    TAG, VERSION, cfg.radius, stats.matureCrops, stats.wildPlants, stats.protectedSeedlings
))
