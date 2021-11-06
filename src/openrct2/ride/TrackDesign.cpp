/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TrackDesign.h"

#include "../Cheats.h"
#include "../Context.h"
#include "../Game.h"
#include "../OpenRCT2.h"
#include "../TrackImporter.h"
#include "../actions/FootpathPlaceFromTrackAction.h"
#include "../actions/FootpathRemoveAction.h"
#include "../actions/LargeSceneryPlaceAction.h"
#include "../actions/LargeSceneryRemoveAction.h"
#include "../actions/MazePlaceTrackAction.h"
#include "../actions/RideCreateAction.h"
#include "../actions/RideEntranceExitPlaceAction.h"
#include "../actions/RideSetNameAction.h"
#include "../actions/RideSetSettingAction.h"
#include "../actions/RideSetVehicleAction.h"
#include "../actions/SmallSceneryPlaceAction.h"
#include "../actions/SmallSceneryRemoveAction.h"
#include "../actions/TrackPlaceAction.h"
#include "../actions/TrackRemoveAction.h"
#include "../actions/WallPlaceAction.h"
#include "../actions/WallRemoveAction.h"
#include "../audio/audio.h"
#include "../core/DataSerialiser.h"
#include "../core/File.h"
#include "../core/Numerics.hpp"
#include "../core/String.hpp"
#include "../drawing/X8DrawingEngine.h"
#include "../localisation/Localisation.h"
#include "../localisation/StringIds.h"
#include "../management/Finance.h"
#include "../network/network.h"
#include "../object/FootpathObject.h"
#include "../object/FootpathSurfaceObject.h"
#include "../object/ObjectList.h"
#include "../object/ObjectManager.h"
#include "../object/ObjectRepository.h"
#include "../rct1/RCT1.h"
#include "../rct1/Tables.h"
#include "../util/SawyerCoding.h"
#include "../util/Util.h"
#include "../world/Footpath.h"
#include "../world/Park.h"
#include "../world/Scenery.h"
#include "../world/SmallScenery.h"
#include "../world/Surface.h"
#include "../world/Wall.h"
#include "Ride.h"
#include "RideData.h"
#include "Track.h"
#include "TrackData.h"
#include "TrackDesign.h"
#include "TrackDesignRepository.h"
#include "Vehicle.h"

#include <algorithm>
#include <iterator>
#include <memory>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
using namespace OpenRCT2::TrackMetaData;

bool gTrackDesignSceneryToggle;
static CoordsXYZ _trackPreviewMin;
static CoordsXYZ _trackPreviewMax;
static CoordsXYZ _trackPreviewOrigin;

bool _trackDesignDrawingPreview;
static uint8_t _trackDesignPlaceOperation;
static int16_t _trackDesignPlaceZ;
static int16_t _trackDesignPlaceSceneryZ;

// Previously all flags in byte_F4414E
static bool _trackDesignPlaceStateEntranceExitPlaced = false;
bool _trackDesignPlaceStateSceneryUnavailable = false;
static bool _trackDesignPlaceStateHasScenery = false;
static bool _trackDesignPlaceStatePlaceScenery = true;
static bool _trackDesignPlaceIsReplay = false;

static void track_design_preview_clear_map();

rct_string_id TrackDesign::CreateTrackDesign(const Ride& ride)
{
    type = ride.type;

    auto object = object_entry_get_object(ObjectType::Ride, ride.subtype);
    if (object != nullptr)
    {
        auto entry = object->GetObjectEntry();
        if (entry.IsEmpty())
        {
            // TODO create a new error message for `JSON objects are unsupported`
            return STR_UNKNOWN_OBJECT_TYPE;
        }
        vehicle_object = ObjectEntryDescriptor(entry);
    }

    ride_mode = ride.mode;
    colour_scheme = ride.colour_scheme_type & 3;

    for (int32_t i = 0; i < RCT2_MAX_CARS_PER_TRAIN; i++)
    {
        vehicle_colours[i].body_colour = ride.vehicle_colours[i].Body;
        vehicle_colours[i].trim_colour = ride.vehicle_colours[i].Trim;
        vehicle_additional_colour[i] = ride.vehicle_colours[i].Ternary;
    }

    for (int32_t i = 0; i < RCT12_NUM_COLOUR_SCHEMES; i++)
    {
        track_spine_colour[i] = ride.track_colour[i].main;
        track_rail_colour[i] = ride.track_colour[i].additional;
        track_support_colour[i] = ride.track_colour[i].supports;
    }

    depart_flags = ride.depart_flags;
    number_of_trains = ride.num_vehicles;
    number_of_cars_per_train = ride.num_cars_per_train;
    min_waiting_time = ride.min_waiting_time;
    max_waiting_time = ride.max_waiting_time;
    operation_setting = ride.operation_option;
    lift_hill_speed = ride.lift_hill_speed;
    num_circuits = ride.num_circuits;

    entrance_style = ride.entrance_style;
    max_speed = static_cast<int8_t>(ride.max_speed / 65536);
    average_speed = static_cast<int8_t>(ride.average_speed / 65536);
    ride_length = ride_get_total_length(&ride) / 65536;
    max_positive_vertical_g = ride.max_positive_vertical_g / 32;
    max_negative_vertical_g = ride.max_negative_vertical_g / 32;
    max_lateral_g = ride.max_lateral_g / 32;
    inversions = ride.holes & 0x1F;
    inversions = ride.inversions & 0x1F;
    inversions |= (ride.sheltered_eighths << 5);
    drops = ride.drops;
    highest_drop_height = ride.highest_drop_height;

    uint16_t totalAirTime = (ride.total_air_time * 123) / 1024;
    if (totalAirTime > 255)
    {
        totalAirTime = 0;
    }
    total_air_time = static_cast<uint8_t>(totalAirTime);

    excitement = ride.ratings.Excitement / 10;
    intensity = ride.ratings.Intensity / 10;
    nausea = ride.ratings.Nausea / 10;

    upkeep_cost = ride.upkeep_cost;
    flags = 0;
    flags2 = 0;

    if (type == RIDE_TYPE_MAZE)
    {
        return CreateTrackDesignMaze(ride);
    }

    return CreateTrackDesignTrack(ride);
}

rct_string_id TrackDesign::CreateTrackDesignTrack(const Ride& ride)
{
    CoordsXYE trackElement;
    if (!ride_try_get_origin_element(&ride, &trackElement))
    {
        return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
    }

    ride_get_start_of_track(&trackElement);

    int32_t z = trackElement.element->GetBaseZ();
    auto trackType = trackElement.element->AsTrack()->GetTrackType();
    uint8_t direction = trackElement.element->GetDirection();
    _saveDirection = direction;
    auto newCoords = GetTrackElementOriginAndApplyChanges(
        { trackElement, z, direction }, trackType, 0, &trackElement.element, 0);

    if (!newCoords.has_value())
    {
        return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
    }
    trackElement.x = newCoords->x;
    trackElement.y = newCoords->y;
    z = newCoords->z;

    const auto& ted = GetTrackElementDescriptor(trackElement.element->AsTrack()->GetTrackType());
    const rct_track_coordinates* trackCoordinates = &ted.Coordinates;
    const auto* trackBlock = ted.Block;
    // Used in the following loop to know when we have
    // completed all of the elements and are back at the
    // start.
    TileElement* initialMap = trackElement.element;

    CoordsXYZ startPos = { trackElement.x, trackElement.y, z + trackCoordinates->z_begin - trackBlock->z };
    _trackPreviewOrigin = startPos;

    do
    {
        TrackDesignTrackElement track{};
        track.type = trackElement.element->AsTrack()->GetTrackType();

        uint8_t trackFlags;
        if (TrackTypeHasSpeedSetting(track.type))
        {
            trackFlags = trackElement.element->AsTrack()->GetBrakeBoosterSpeed() >> 1;
        }
        else
        {
            trackFlags = trackElement.element->AsTrack()->GetSeatRotation();
        }

        if (trackElement.element->AsTrack()->HasChain())
            trackFlags |= RCT12_TRACK_ELEMENT_TYPE_FLAG_CHAIN_LIFT;
        trackFlags |= trackElement.element->AsTrack()->GetColourScheme() << 4;
        if (ride.GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_HAS_ALTERNATIVE_TRACK_TYPE)
            && trackElement.element->AsTrack()->IsInverted())
        {
            trackFlags |= TD6_TRACK_ELEMENT_FLAG_INVERTED;
        }

        track.flags = trackFlags;
        track_elements.push_back(track);

        if (!track_block_get_next(&trackElement, &trackElement, nullptr, nullptr))
        {
            break;
        }

        z = trackElement.element->GetBaseZ();
        direction = trackElement.element->GetDirection();
        trackType = trackElement.element->AsTrack()->GetTrackType();
        newCoords = GetTrackElementOriginAndApplyChanges(
            { trackElement, z, direction }, trackType, 0, &trackElement.element, 0);

        if (!newCoords.has_value())
        {
            break;
        }
        trackElement.x = newCoords->x;
        trackElement.y = newCoords->y;

        if (track_elements.size() > TD6MaxTrackElements)
        {
            return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
        }
    } while (trackElement.element != initialMap);

    // First entrances, second exits
    for (int32_t i = 0; i < 2; i++)
    {
        for (StationIndex station_index = 0; station_index < RCT12_MAX_STATIONS_PER_RIDE; station_index++)
        {
            z = ride.stations[station_index].GetBaseZ();

            TileCoordsXYZD location;
            if (i == 0)
            {
                location = ride_get_entrance_location(&ride, station_index);
            }
            else
            {
                location = ride_get_exit_location(&ride, station_index);
            }

            if (location.IsNull())
            {
                continue;
            }

            CoordsXY mapLocation = location.ToCoordsXY();

            TileElement* tileElement = map_get_first_element_at(mapLocation);
            if (tileElement == nullptr)
                continue;

            do
            {
                if (tileElement->GetType() != TILE_ELEMENT_TYPE_ENTRANCE)
                    continue;
                if (tileElement->GetBaseZ() == z)
                    break;
            } while (!(tileElement++)->IsLastForTile());

            // Add something that stops this from walking off the end

            Direction entranceDirection = tileElement->GetDirection();
            entranceDirection -= _saveDirection;
            entranceDirection &= TILE_ELEMENT_DIRECTION_MASK;

            TrackDesignEntranceElement entrance{};
            entrance.direction = entranceDirection;

            mapLocation -= _trackPreviewOrigin;

            // Rotate entrance coordinates backwards to the correct direction
            auto rotatedMapLocation = mapLocation.Rotate(0 - _saveDirection);
            entrance.x = rotatedMapLocation.x;
            entrance.y = rotatedMapLocation.y;

            z -= _trackPreviewOrigin.z;
            z /= 8;

            if (z > 127 || z < -126)
            {
                return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
            }

            entrance.z = z;

            // If this is the exit version
            if (i == 1)
            {
                entrance.isExit = true;
            }
            entrance_elements.push_back(entrance);
        }
    }

    place_virtual_track(this, PTD_OPERATION_DRAW_OUTLINES, true, GetOrAllocateRide(PreviewRideId), { 4096, 4096, 0 });

    // Resave global vars for scenery reasons.
    _trackPreviewOrigin = startPos;

    gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE_CONSTRUCT;
    gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE_ARROW;
    gMapSelectFlags &= ~MAP_SELECT_FLAG_GREEN;

    space_required_x = ((_trackPreviewMax.x - _trackPreviewMin.x) / 32) + 1;
    space_required_y = ((_trackPreviewMax.y - _trackPreviewMin.y) / 32) + 1;
    return STR_NONE;
}

rct_string_id TrackDesign::CreateTrackDesignMaze(const Ride& ride)
{
    auto startLoc = MazeGetFirstElement(ride);

    if (startLoc.element == nullptr)
    {
        return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
    }

    _trackPreviewOrigin = { startLoc.x, startLoc.y, startLoc.element->GetBaseZ() };

    // x is defined here as we can start the search
    // on tile start_x, start_y but then the next row
    // must restart on 0
    for (int32_t y = startLoc.y, x = startLoc.x; y < MAXIMUM_MAP_SIZE_BIG; y += COORDS_XY_STEP)
    {
        for (; x < MAXIMUM_MAP_SIZE_BIG; x += COORDS_XY_STEP)
        {
            auto tileElement = map_get_first_element_at(CoordsXY{ x, y });
            do
            {
                if (tileElement == nullptr)
                    break;
                if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
                    continue;
                if (tileElement->AsTrack()->GetRideIndex() != ride.id)
                    continue;

                TrackDesignMazeElement maze{};

                maze.maze_entry = tileElement->AsTrack()->GetMazeEntry();
                maze.x = (x - startLoc.x) / COORDS_XY_STEP;
                maze.y = (y - startLoc.y) / COORDS_XY_STEP;
                _saveDirection = tileElement->GetDirection();
                maze_elements.push_back(maze);

                if (maze_elements.size() >= 2000)
                {
                    return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
                }
            } while (!(tileElement++)->IsLastForTile());
        }
        x = 0;
    }

    auto location = ride_get_entrance_location(&ride, 0);
    if (location.IsNull())
    {
        return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
    }

    CoordsXY entranceLoc = location.ToCoordsXY();
    auto tileElement = map_get_first_element_at(entranceLoc);
    do
    {
        if (tileElement == nullptr)
            return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_ENTRANCE)
            continue;
        if (tileElement->AsEntrance()->GetEntranceType() != ENTRANCE_TYPE_RIDE_ENTRANCE)
            continue;
        if (tileElement->AsEntrance()->GetRideIndex() == ride.id)
            break;
    } while (!(tileElement++)->IsLastForTile());
    // Add something that stops this from walking off the end

    uint8_t entranceDirection = tileElement->GetDirection();
    TrackDesignMazeElement mazeEntrance{};
    mazeEntrance.direction = entranceDirection;
    mazeEntrance.type = 8;
    mazeEntrance.x = static_cast<int8_t>((entranceLoc.x - startLoc.x) / 32);
    mazeEntrance.y = static_cast<int8_t>((entranceLoc.y - startLoc.y) / 32);
    maze_elements.push_back(mazeEntrance);

    location = ride_get_exit_location(&ride, 0);
    if (location.IsNull())
    {
        return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
    }

    CoordsXY exitLoc = location.ToCoordsXY();
    tileElement = map_get_first_element_at(exitLoc);
    if (tileElement == nullptr)
        return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
    do
    {
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_ENTRANCE)
            continue;
        if (tileElement->AsEntrance()->GetEntranceType() != ENTRANCE_TYPE_RIDE_EXIT)
            continue;
        if (tileElement->AsEntrance()->GetRideIndex() == ride.id)
            break;
    } while (!(tileElement++)->IsLastForTile());
    // Add something that stops this from walking off the end

    uint8_t exit_direction = tileElement->GetDirection();
    TrackDesignMazeElement mazeExit{};
    mazeExit.direction = exit_direction;
    mazeExit.type = 0x80;
    mazeExit.x = static_cast<int8_t>((exitLoc.x - startLoc.x) / 32);
    mazeExit.y = static_cast<int8_t>((exitLoc.y - startLoc.y) / 32);
    maze_elements.push_back(mazeExit);

    // Save global vars as they are still used by scenery????
    int32_t startZ = _trackPreviewOrigin.z;
    place_virtual_track(this, PTD_OPERATION_DRAW_OUTLINES, true, GetOrAllocateRide(PreviewRideId), { 4096, 4096, 0 });
    _trackPreviewOrigin = { startLoc.x, startLoc.y, startZ };

    gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE_CONSTRUCT;
    gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE_ARROW;
    gMapSelectFlags &= ~MAP_SELECT_FLAG_GREEN;

    space_required_x = ((_trackPreviewMax.x - _trackPreviewMin.x) / 32) + 1;
    space_required_y = ((_trackPreviewMax.y - _trackPreviewMin.y) / 32) + 1;
    return STR_NONE;
}

CoordsXYE TrackDesign::MazeGetFirstElement(const Ride& ride)
{
    CoordsXYE tile{};
    for (tile.y = 0; tile.y < MAXIMUM_MAP_SIZE_BIG; tile.y += COORDS_XY_STEP)
    {
        for (tile.x = 0; tile.x < MAXIMUM_MAP_SIZE_BIG; tile.x += COORDS_XY_STEP)
        {
            tile.element = map_get_first_element_at(CoordsXY{ tile.x, tile.y });
            do
            {
                if (tile.element == nullptr)
                    break;

                if (tile.element->GetType() != TILE_ELEMENT_TYPE_TRACK)
                    continue;
                if (tile.element->AsTrack()->GetRideIndex() == ride.id)
                {
                    return tile;
                }
            } while (!(tile.element++)->IsLastForTile());
        }
    }
    tile.element = nullptr;
    return tile;
}

rct_string_id TrackDesign::CreateTrackDesignScenery()
{
    scenery_elements = _trackSavedTileElementsDesc;
    // Run an element loop
    for (auto& scenery : scenery_elements)
    {
        switch (scenery.scenery_object.GetType())
        {
            case ObjectType::Paths:
            {
                uint8_t slope = (scenery.flags & 0x60) >> 5;
                slope -= _saveDirection;

                scenery.flags &= 0x9F;
                scenery.flags |= ((slope & 3) << 5);

                // Direction of connection on path
                uint8_t direction = scenery.flags & 0xF;
                // Rotate the direction by the track direction
                direction = ((direction << 4) >> _saveDirection);

                scenery.flags &= 0xF0;
                scenery.flags |= (direction & 0xF) | (direction >> 4);
                break;
            }
            case ObjectType::Walls:
            {
                uint8_t direction = scenery.flags & 3;
                direction -= _saveDirection;

                scenery.flags &= 0xFC;
                scenery.flags |= (direction & 3);
                break;
            }
            default:
            {
                uint8_t direction = scenery.flags & 3;
                uint8_t quadrant = (scenery.flags & 0x0C) >> 2;

                direction -= _saveDirection;
                quadrant -= _saveDirection;

                scenery.flags &= 0xF0;
                scenery.flags |= (direction & 3) | ((quadrant & 3) << 2);
                break;
            }
        }

        // Cast the value into a uint8_t as this value is not signed yet.
        auto sceneryPos = TileCoordsXY(static_cast<uint8_t>(scenery.x), static_cast<uint8_t>(scenery.y)).ToCoordsXY();
        CoordsXY sceneryMapPos = sceneryPos - _trackPreviewOrigin;
        CoordsXY rotatedSceneryMapPos = sceneryMapPos.Rotate(0 - _saveDirection);
        TileCoordsXY sceneryTilePos{ rotatedSceneryMapPos };

        if (sceneryTilePos.x > 127 || sceneryTilePos.y > 127 || sceneryTilePos.x < -126 || sceneryTilePos.y < -126)
        {
            return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
        }

        scenery.x = static_cast<int8_t>(sceneryTilePos.x);
        scenery.y = static_cast<int8_t>(sceneryTilePos.y);

        int32_t z = scenery.z * COORDS_Z_STEP - _trackPreviewOrigin.z;
        z /= COORDS_Z_STEP;
        if (z > 127 || z < -126)
        {
            return STR_TRACK_TOO_LARGE_OR_TOO_MUCH_SCENERY;
        }
        scenery.z = z;
    }

    return STR_NONE;
}

void TrackDesign::Serialise(DataSerialiser& stream)
{
    if (stream.IsLogging())
    {
        stream << DS_TAG(name);
        // There is too much information logged.
        // See sub actions for this information if required.
        return;
    }
    stream << DS_TAG(type);
    stream << DS_TAG(vehicle_type);
    stream << DS_TAG(cost);
    stream << DS_TAG(flags);
    stream << DS_TAG(ride_mode);
    stream << DS_TAG(track_flags);
    stream << DS_TAG(colour_scheme);
    stream << DS_TAG(vehicle_colours);
    stream << DS_TAG(entrance_style);
    stream << DS_TAG(total_air_time);
    stream << DS_TAG(depart_flags);
    stream << DS_TAG(number_of_trains);
    stream << DS_TAG(number_of_cars_per_train);
    stream << DS_TAG(min_waiting_time);
    stream << DS_TAG(max_waiting_time);
    stream << DS_TAG(operation_setting);
    stream << DS_TAG(max_speed);
    stream << DS_TAG(average_speed);
    stream << DS_TAG(ride_length);
    stream << DS_TAG(max_positive_vertical_g);
    stream << DS_TAG(max_negative_vertical_g);
    stream << DS_TAG(max_lateral_g);
    stream << DS_TAG(inversions);
    stream << DS_TAG(holes);
    stream << DS_TAG(drops);
    stream << DS_TAG(highest_drop_height);
    stream << DS_TAG(excitement);
    stream << DS_TAG(intensity);
    stream << DS_TAG(nausea);
    stream << DS_TAG(upkeep_cost);
    stream << DS_TAG(track_spine_colour);
    stream << DS_TAG(track_rail_colour);
    stream << DS_TAG(track_support_colour);
    stream << DS_TAG(flags2);
    stream << DS_TAG(vehicle_object.Entry);
    stream << DS_TAG(space_required_x);
    stream << DS_TAG(space_required_y);
    stream << DS_TAG(vehicle_additional_colour);
    stream << DS_TAG(lift_hill_speed);
    stream << DS_TAG(num_circuits);

    stream << DS_TAG(maze_elements);
    stream << DS_TAG(track_elements);
    stream << DS_TAG(entrance_elements);
    stream << DS_TAG(scenery_elements);

    stream << DS_TAG(name);
}

std::unique_ptr<TrackDesign> track_design_open(const utf8* path)
{
    try
    {
        auto trackImporter = TrackImporter::Create(path);
        trackImporter->Load(path);
        return trackImporter->Import();
    }
    catch (const std::exception& e)
    {
        log_error("Unable to load track design: %s", e.what());
    }
    log_verbose("track_design_open(\"%s\")", path);
    return nullptr;
}

/**
 *
 *  rct2: 0x006ABDB0
 */
static void track_design_load_scenery_objects(TrackDesign* td6)
{
    auto& objectManager = OpenRCT2::GetContext()->GetObjectManager();
    objectManager.UnloadAll();

    // Load ride object
    if (td6->vehicle_object.HasValue())
    {
        objectManager.LoadObject(td6->vehicle_object);
    }

    // Load scenery objects
    for (const auto& scenery : td6->scenery_elements)
    {
        if (scenery.scenery_object.HasValue())
        {
            objectManager.LoadObject(scenery.scenery_object);
        }
    }
}

struct TrackSceneryEntry
{
    ObjectType Type = ObjectType::None;
    ObjectEntryIndex Index = OBJECT_ENTRY_INDEX_NULL;
    ObjectEntryIndex SecondaryIndex = OBJECT_ENTRY_INDEX_NULL; // For footpath railing
};

static ObjectEntryIndex TrackDesignGetDefaultSurfaceIndex(bool isQueue)
{
    for (ObjectEntryIndex i = 0; i < MAX_FOOTPATH_SURFACE_OBJECTS; i++)
    {
        auto footpathSurfaceObj = GetPathSurfaceEntry(i);
        if (footpathSurfaceObj != nullptr)
        {
            if (footpathSurfaceObj->Flags & FOOTPATH_ENTRY_FLAG_SHOW_ONLY_IN_SCENARIO_EDITOR)
            {
                continue;
            }

            if (isQueue != ((footpathSurfaceObj->Flags & FOOTPATH_ENTRY_FLAG_IS_QUEUE) != 0))
            {
                continue;
            }

            return i;
        }
    }
    return OBJECT_ENTRY_INDEX_NULL;
}

static ObjectEntryIndex TrackDesignGetDefaultRailingIndex()
{
    for (ObjectEntryIndex i = 0; i < MAX_FOOTPATH_RAILINGS_OBJECTS; i++)
    {
        auto footpathRailingsObj = GetPathRailingsEntry(i);
        if (footpathRailingsObj != nullptr)
        {
            return i;
        }
    }
    return OBJECT_ENTRY_INDEX_NULL;
}

static ObjectEntryIndex TrackDesignGetDefaultPathIndex(bool isQueue)
{
    for (ObjectEntryIndex i = 0; i < MAX_PATH_OBJECTS; i++)
    {
        auto legacyPathEntry = GetLegacyFootpathEntry(i);
        if (legacyPathEntry != nullptr)
        {
            const auto& surfaceDescriptor = isQueue ? legacyPathEntry->GetQueueSurfaceDescriptor()
                                                    : legacyPathEntry->GetPathSurfaceDescriptor();
            if (surfaceDescriptor.IsEditorOnly())
            {
                continue;
            }
            return i;
        }
    }
    return OBJECT_ENTRY_INDEX_NULL;
}

static std::optional<TrackSceneryEntry> TrackDesignPlaceSceneryElementGetEntry(const TrackDesignSceneryElement& scenery)
{
    TrackSceneryEntry result;

    auto& objectMgr = OpenRCT2::GetContext()->GetObjectManager();
    if (scenery.scenery_object.GetType() == ObjectType::Paths)
    {
        auto footpathMapping = GetFootpathSurfaceId(scenery.scenery_object, true, scenery.IsQueue());
        if (footpathMapping == nullptr)
        {
            // Check if legacy path object is loaded
            auto obj = objectMgr.GetLoadedObject(scenery.scenery_object);
            if (obj != nullptr)
            {
                result.Type = obj->GetObjectType();
                result.Index = objectMgr.GetLoadedObjectEntryIndex(obj);
            }
            else
            {
                result.Type = ObjectType::FootpathSurface;
            }
        }
        else
        {
            result.Type = ObjectType::FootpathSurface;
            result.Index = objectMgr.GetLoadedObjectEntryIndex(
                ObjectEntryDescriptor(scenery.IsQueue() ? footpathMapping->QueueSurface : footpathMapping->NormalSurface));
            result.SecondaryIndex = objectMgr.GetLoadedObjectEntryIndex(ObjectEntryDescriptor(footpathMapping->Railing));
        }

        if (result.Index == OBJECT_ENTRY_INDEX_NULL)
            result.Index = TrackDesignGetDefaultSurfaceIndex(scenery.IsQueue());
        if (result.SecondaryIndex == OBJECT_ENTRY_INDEX_NULL)
            result.SecondaryIndex = TrackDesignGetDefaultRailingIndex();

        // NOTE: This block can be deleted in the NSF branch.
        if (result.Index == OBJECT_ENTRY_INDEX_NULL)
        {
            result.Type = ObjectType::Paths;
            result.Index = TrackDesignGetDefaultPathIndex(scenery.IsQueue());
        }

        if (result.Index == OBJECT_ENTRY_INDEX_NULL)
        {
            _trackDesignPlaceStateSceneryUnavailable = true;
            return {};
        }
    }
    else
    {
        auto obj = objectMgr.GetLoadedObject(scenery.scenery_object);
        if (obj != nullptr)
        {
            result.Type = obj->GetObjectType();
            result.Index = objectMgr.GetLoadedObjectEntryIndex(obj);
        }
        else
        {
            _trackDesignPlaceStateSceneryUnavailable = true;
            return {};
        }
    }
    return result;
}

/**
 *
 *  rct2: 0x006D247A
 */
static void track_design_mirror_scenery(TrackDesign* td6)
{
    auto& objectMgr = OpenRCT2::GetContext()->GetObjectManager();
    for (auto& scenery : td6->scenery_elements)
    {
        auto entryInfo = TrackDesignPlaceSceneryElementGetEntry(scenery);
        if (!entryInfo)
            continue;

        auto obj = objectMgr.GetLoadedObject(entryInfo->Type, entryInfo->Index);
        switch (obj->GetObjectType())
        {
            case ObjectType::LargeScenery:
            {
                auto* sceneryEntry = reinterpret_cast<const LargeSceneryEntry*>(obj->GetLegacyData());
                int16_t x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                for (rct_large_scenery_tile* tile = sceneryEntry->tiles; tile->x_offset != -1; tile++)
                {
                    if (x1 > tile->x_offset)
                    {
                        x1 = tile->x_offset;
                    }
                    if (x2 < tile->x_offset)
                    {
                        x2 = tile->x_offset;
                    }
                    if (y1 > tile->y_offset)
                    {
                        y1 = tile->y_offset;
                    }
                    if (y2 < tile->y_offset)
                    {
                        y2 = tile->y_offset;
                    }
                }

                switch (scenery.flags & 3)
                {
                    case 0:
                        scenery.y = (-(scenery.y * 32 + y1) - y2) / 32;
                        break;
                    case 1:
                        scenery.x = (scenery.x * 32 + y2 + y1) / 32;
                        scenery.y = (-(scenery.y * 32)) / 32;
                        scenery.flags ^= (1 << 1);
                        break;
                    case 2:
                        scenery.y = (-(scenery.y * 32 - y2) + y1) / 32;
                        break;
                    case 3:
                        scenery.x = (scenery.x * 32 - y2 - y1) / 32;
                        scenery.y = (-(scenery.y * 32)) / 32;
                        scenery.flags ^= (1 << 1);
                        break;
                }
                break;
            }
            case ObjectType::SmallScenery:
            {
                auto* sceneryEntry = reinterpret_cast<const SmallSceneryEntry*>(obj->GetLegacyData());
                scenery.y = -scenery.y;

                if (sceneryEntry->HasFlag(SMALL_SCENERY_FLAG_DIAGONAL))
                {
                    scenery.flags ^= (1 << 0);
                    if (!sceneryEntry->HasFlag(SMALL_SCENERY_FLAG_FULL_TILE))
                    {
                        scenery.flags ^= (1 << 2);
                    }
                    break;
                }
                if (scenery.flags & (1 << 0))
                {
                    scenery.flags ^= (1 << 1);
                }

                scenery.flags ^= (1 << 2);
                break;
            }
            case ObjectType::Walls:
            {
                scenery.y = -scenery.y;
                if (scenery.flags & (1 << 0))
                {
                    scenery.flags ^= (1 << 1);
                }
                break;
            }
            case ObjectType::Paths:
            case ObjectType::FootpathSurface:
            {
                scenery.y = -scenery.y;

                if (scenery.flags & (1 << 5))
                {
                    scenery.flags ^= (1 << 6);
                }

                uint8_t flags = scenery.flags;
                flags = ((flags & (1 << 3)) >> 2) | ((flags & (1 << 1)) << 2);
                scenery.flags &= 0xF5;
                scenery.flags |= flags;
                break;
            }
            default:
                break;
        }
    }
}

/**
 *
 *  rct2: 0x006D2443
 */
static void track_design_mirror_ride(TrackDesign* td6)
{
    for (auto& track : td6->track_elements)
    {
        const auto& ted = GetTrackElementDescriptor(track.type);
        track.type = ted.MirrorElement;
    }

    for (auto& entrance : td6->entrance_elements)
    {
        entrance.y = -entrance.y;
        if (entrance.direction & 1)
        {
            entrance.direction = direction_reverse(entrance.direction);
        }
    }
}

/** rct2: 0x00993EDC */
static constexpr const uint8_t maze_segment_mirror_map[] = {
    5, 4, 2, 7, 1, 0, 14, 3, 13, 12, 10, 15, 9, 8, 6, 11,
};

/**
 *
 *  rct2: 0x006D25FA
 */
static void track_design_mirror_maze(TrackDesign* td6)
{
    for (auto& maze : td6->maze_elements)
    {
        maze.y = -maze.y;

        if (maze.type == 0x8 || maze.type == 0x80)
        {
            if (maze.direction & 1)
            {
                maze.direction = direction_reverse(maze.direction);
            }
            continue;
        }

        uint16_t maze_entry = maze.maze_entry;
        uint16_t new_entry = 0;
        for (uint8_t position = bitscanforward(maze_entry); position != 0xFF; position = bitscanforward(maze_entry))
        {
            maze_entry &= ~(1 << position);
            new_entry |= (1 << maze_segment_mirror_map[position]);
        }
        maze.maze_entry = new_entry;
    }
}

/**
 *
 *  rct2: 0x006D2436
 */
void track_design_mirror(TrackDesign* td6)
{
    if (td6->type == RIDE_TYPE_MAZE)
    {
        track_design_mirror_maze(td6);
    }
    else
    {
        track_design_mirror_ride(td6);
    }
    track_design_mirror_scenery(td6);
}

static void track_design_add_selection_tile(const CoordsXY& coords)
{
    auto tileIterator = std::find(gMapSelectionTiles.begin(), gMapSelectionTiles.end(), coords);
    if (tileIterator == gMapSelectionTiles.end())
    {
        gMapSelectionTiles.push_back(coords);
    }
}

static void track_design_update_max_min_coordinates(const CoordsXYZ& coords)
{
    _trackPreviewMin = { std::min(_trackPreviewMin.x, coords.x), std::min(_trackPreviewMin.y, coords.y),
                         std::min(_trackPreviewMin.z, coords.z) };
    _trackPreviewMax = { std::max(_trackPreviewMax.x, coords.x), std::max(_trackPreviewMax.y, coords.y),
                         std::max(_trackPreviewMax.z, coords.z) };
}

static bool TrackDesignPlaceSceneryElementRemoveGhost(
    CoordsXY mapCoord, const TrackDesignSceneryElement& scenery, uint8_t rotation, int32_t originZ)
{
    auto entryInfo = TrackDesignPlaceSceneryElementGetEntry(scenery);
    if (!entryInfo)
    {
        return true;
    }

    if (_trackDesignPlaceStateSceneryUnavailable)
    {
        return true;
    }

    int32_t z = (scenery.z * COORDS_Z_STEP) + originZ;
    uint8_t sceneryRotation = (rotation + scenery.flags) & TILE_ELEMENT_DIRECTION_MASK;
    const uint32_t flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
        | GAME_COMMAND_FLAG_GHOST;
    std::unique_ptr<GameAction> ga;
    switch (entryInfo->Type)
    {
        case ObjectType::SmallScenery:
        {
            uint8_t quadrant = (scenery.flags >> 2) + _currentTrackPieceDirection;
            quadrant &= 3;

            auto* sceneryEntry = get_small_scenery_entry(entryInfo->Index);
            if (!(!sceneryEntry->HasFlag(SMALL_SCENERY_FLAG_FULL_TILE) && sceneryEntry->HasFlag(SMALL_SCENERY_FLAG_DIAGONAL))
                && sceneryEntry->HasFlag(
                    SMALL_SCENERY_FLAG_DIAGONAL | SMALL_SCENERY_FLAG_HALF_SPACE | SMALL_SCENERY_FLAG_THREE_QUARTERS))
            {
                quadrant = 0;
            }

            ga = std::make_unique<SmallSceneryRemoveAction>(CoordsXYZ{ mapCoord.x, mapCoord.y, z }, quadrant, entryInfo->Index);
            break;
        }
        case ObjectType::LargeScenery:
            ga = std::make_unique<LargeSceneryRemoveAction>(CoordsXYZD{ mapCoord.x, mapCoord.y, z, sceneryRotation }, 0);
            break;
        case ObjectType::Walls:
            ga = std::make_unique<WallRemoveAction>(CoordsXYZD{ mapCoord.x, mapCoord.y, z, sceneryRotation });
            break;
        case ObjectType::Paths:
        case ObjectType::FootpathSurface:
            ga = std::make_unique<FootpathRemoveAction>(CoordsXYZ{ mapCoord.x, mapCoord.y, z });
            break;
        default:
            return true;
    }
    ga->SetFlags(flags);
    GameActions::ExecuteNested(ga.get());
    return true;
}

static bool TrackDesignPlaceSceneryElementGetPlaceZ(const TrackDesignSceneryElement& scenery)
{
    int32_t z = scenery.z * COORDS_Z_STEP + _trackDesignPlaceZ;
    if (z < _trackDesignPlaceSceneryZ)
    {
        _trackDesignPlaceSceneryZ = z;
    }

    TrackDesignPlaceSceneryElementGetEntry(scenery);
    return true;
}

static std::optional<money32> TrackDesignPlaceSceneryElement(
    CoordsXY mapCoord, uint8_t mode, const TrackDesignSceneryElement& scenery, uint8_t rotation, int32_t originZ)
{
    if (_trackDesignPlaceOperation == PTD_OPERATION_DRAW_OUTLINES && mode == 0)
    {
        track_design_add_selection_tile(mapCoord);
        return 0;
    }

    if (_trackDesignPlaceOperation == PTD_OPERATION_REMOVE_GHOST && mode == 0)
    {
        if (TrackDesignPlaceSceneryElementRemoveGhost(mapCoord, scenery, rotation, originZ))
            return 0;

        return std::nullopt;
    }

    if (_trackDesignPlaceOperation == PTD_OPERATION_GET_PLACE_Z)
    {
        if (TrackDesignPlaceSceneryElementGetPlaceZ(scenery))
            return 0;

        return std::nullopt;
    }

    money32 totalCost = 0;
    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY || _trackDesignPlaceOperation == PTD_OPERATION_PLACE
        || _trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST
        || _trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
    {
        auto entryInfo = TrackDesignPlaceSceneryElementGetEntry(scenery);
        if (!entryInfo)
        {
            return 0;
        }

        money32 cost;
        int16_t z;
        uint8_t flags;
        uint8_t quadrant;

        switch (entryInfo->Type)
        {
            case ObjectType::SmallScenery:
            {
                if (mode != 0)
                {
                    return 0;
                }
                if (_trackDesignPlaceOperation == PTD_OPERATION_GET_PLACE_Z)
                {
                    return 0;
                }

                rotation += scenery.flags;
                rotation &= 3;
                z = scenery.z * COORDS_Z_STEP + originZ;
                quadrant = ((scenery.flags >> 2) + _currentTrackPieceDirection) & 3;

                flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_PATH_SCENERY;
                if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                {
                    flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_PATH_SCENERY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED
                        | GAME_COMMAND_FLAG_NO_SPEND;
                }
                else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                {
                    flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_PATH_SCENERY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED
                        | GAME_COMMAND_FLAG_GHOST | GAME_COMMAND_FLAG_NO_SPEND;
                }
                else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                {
                    flags = GAME_COMMAND_FLAG_PATH_SCENERY;
                }
                if (_trackDesignPlaceIsReplay)
                {
                    flags |= GAME_COMMAND_FLAG_REPLAY;
                }
                gGameCommandErrorTitle = STR_CANT_POSITION_THIS_HERE;

                auto smallSceneryPlace = SmallSceneryPlaceAction(
                    { mapCoord.x, mapCoord.y, z, rotation }, quadrant, entryInfo->Index, scenery.primary_colour,
                    scenery.secondary_colour);

                smallSceneryPlace.SetFlags(flags);
                auto res = flags & GAME_COMMAND_FLAG_APPLY ? GameActions::ExecuteNested(&smallSceneryPlace)
                                                           : GameActions::QueryNested(&smallSceneryPlace);

                cost = res->Error == GameActions::Status::Ok ? res->Cost : 0;
                break;
            }
            case ObjectType::LargeScenery:
            {
                if (mode != 0)
                {
                    return 0;
                }
                if (_trackDesignPlaceOperation == PTD_OPERATION_GET_PLACE_Z)
                {
                    return 0;
                }

                rotation += scenery.flags;
                rotation &= 3;

                z = scenery.z * COORDS_Z_STEP + originZ;

                flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_PATH_SCENERY;
                if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                {
                    flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_PATH_SCENERY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED
                        | GAME_COMMAND_FLAG_NO_SPEND;
                }
                else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                {
                    flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_PATH_SCENERY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED
                        | GAME_COMMAND_FLAG_GHOST | GAME_COMMAND_FLAG_NO_SPEND;
                }
                else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                {
                    flags = GAME_COMMAND_FLAG_PATH_SCENERY;
                }
                if (_trackDesignPlaceIsReplay)
                {
                    flags |= GAME_COMMAND_FLAG_REPLAY;
                }
                auto sceneryPlaceAction = LargeSceneryPlaceAction(
                    { mapCoord.x, mapCoord.y, z, rotation }, entryInfo->Index, scenery.primary_colour,
                    scenery.secondary_colour);
                sceneryPlaceAction.SetFlags(flags);
                auto res = flags & GAME_COMMAND_FLAG_APPLY ? GameActions::ExecuteNested(&sceneryPlaceAction)
                                                           : GameActions::QueryNested(&sceneryPlaceAction);

                cost = res->Cost;
                break;
            }
            case ObjectType::Walls:
            {
                if (mode != 0)
                {
                    return 0;
                }
                if (_trackDesignPlaceOperation == PTD_OPERATION_GET_PLACE_Z)
                {
                    return 0;
                }

                z = scenery.z * COORDS_Z_STEP + originZ;
                rotation += scenery.flags;
                rotation &= 3;

                flags = GAME_COMMAND_FLAG_APPLY;
                if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                {
                    flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_PATH_SCENERY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED
                        | GAME_COMMAND_FLAG_NO_SPEND;
                }
                else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                {
                    flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
                        | GAME_COMMAND_FLAG_GHOST;
                }
                else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                {
                    flags = 0;
                }
                if (_trackDesignPlaceIsReplay)
                {
                    flags |= GAME_COMMAND_FLAG_REPLAY;
                }
                auto wallPlaceAction = WallPlaceAction(
                    entryInfo->Index, { mapCoord.x, mapCoord.y, z }, rotation, scenery.primary_colour, scenery.secondary_colour,
                    (scenery.flags & 0xFC) >> 2);
                wallPlaceAction.SetFlags(flags);
                auto res = flags & GAME_COMMAND_FLAG_APPLY ? GameActions::ExecuteNested(&wallPlaceAction)
                                                           : GameActions::QueryNested(&wallPlaceAction);

                cost = res->Cost;
                break;
            }
            case ObjectType::Paths:
            case ObjectType::FootpathSurface:
                if (_trackDesignPlaceOperation == PTD_OPERATION_GET_PLACE_Z)
                {
                    return 0;
                }

                z = (scenery.z * COORDS_Z_STEP + originZ) / COORDS_Z_STEP;
                if (mode == 0)
                {
                    auto isQueue = scenery.IsQueue();

                    uint8_t bh = ((scenery.flags & 0xF) << rotation);
                    flags = bh >> 4;
                    bh = (bh | flags) & 0xF;
                    flags = (((scenery.flags >> 5) + rotation) & 3) << 5;
                    bh |= flags;

                    bh |= scenery.flags & 0x90;

                    flags = GAME_COMMAND_FLAG_APPLY;
                    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                    {
                        flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND;
                    }
                    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                    {
                        flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
                            | GAME_COMMAND_FLAG_GHOST;
                    }
                    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                    {
                        flags = 0;
                    }
                    if (_trackDesignPlaceIsReplay)
                    {
                        flags |= GAME_COMMAND_FLAG_REPLAY;
                    }
                    uint8_t slope = ((bh >> 5) & 0x3) | ((bh >> 2) & 0x4);
                    uint8_t edges = bh & 0xF;
                    PathConstructFlags constructFlags = 0;
                    if (isQueue)
                        constructFlags |= PathConstructFlag::IsQueue;
                    if (entryInfo->Type == ObjectType::Paths)
                        constructFlags |= PathConstructFlag::IsLegacyPathObject;
                    auto footpathPlaceAction = FootpathPlaceFromTrackAction(
                        { mapCoord.x, mapCoord.y, z * COORDS_Z_STEP }, slope, entryInfo->Index, entryInfo->SecondaryIndex,
                        edges, constructFlags);
                    footpathPlaceAction.SetFlags(flags);
                    auto res = flags & GAME_COMMAND_FLAG_APPLY ? GameActions::ExecuteNested(&footpathPlaceAction)
                                                               : GameActions::QueryNested(&footpathPlaceAction);
                    // Ignore failures
                    cost = res->Error == GameActions::Status::Ok ? res->Cost : 0;
                }
                else
                {
                    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                    {
                        return 0;
                    }

                    auto* pathElement = map_get_path_element_at({ mapCoord.x / 32, mapCoord.y / 32, z });

                    if (pathElement == nullptr)
                    {
                        return 0;
                    }

                    footpath_queue_chain_reset();
                    footpath_remove_edges_at(mapCoord, reinterpret_cast<TileElement*>(pathElement));

                    flags = GAME_COMMAND_FLAG_APPLY;
                    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                    {
                        flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND;
                    }
                    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                    {
                        flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
                            | GAME_COMMAND_FLAG_GHOST;
                    }
                    if (_trackDesignPlaceIsReplay)
                    {
                        flags |= GAME_COMMAND_FLAG_REPLAY;
                    }
                    footpath_connect_edges(mapCoord, reinterpret_cast<TileElement*>(pathElement), flags);
                    footpath_update_queue_chains();
                    return 0;
                }
                break;
            default:
                _trackDesignPlaceStateSceneryUnavailable = true;
                return 0;
        }
        totalCost = add_clamp_money32(totalCost, cost);
        if (_trackDesignPlaceOperation != PTD_OPERATION_PLACE)
        {
            if (cost == MONEY32_UNDEFINED)
            {
                totalCost = MONEY32_UNDEFINED;
            }
        }
        if (totalCost != MONEY32_UNDEFINED)
        {
            return totalCost;
        }
        if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE)
        {
            return totalCost;
        }
        return std::nullopt;
    }
    return totalCost;
}

/**
 *
 *  rct2: 0x006D0964
 */
static std::optional<money32> track_design_place_all_scenery(
    const std::vector<TrackDesignSceneryElement>& sceneryList, const CoordsXYZ& origin)
{
    money32 cost = 0;

    for (uint8_t mode = 0; mode <= 1; mode++)
    {
        if (!sceneryList.empty())
        {
            _trackDesignPlaceStateHasScenery = true;
        }

        if (!_trackDesignPlaceStatePlaceScenery)
        {
            continue;
        }

        for (const auto& scenery : sceneryList)
        {
            uint8_t rotation = _currentTrackPieceDirection;
            TileCoordsXY tileCoords = TileCoordsXY(origin);
            TileCoordsXY offsets = { scenery.x, scenery.y };
            tileCoords += offsets.Rotate(rotation);

            auto mapCoord = CoordsXYZ{ tileCoords.ToCoordsXY(), origin.z };
            track_design_update_max_min_coordinates(mapCoord);

            auto placementCost = TrackDesignPlaceSceneryElement(mapCoord, mode, scenery, rotation, origin.z);
            if (!placementCost.has_value() || placementCost == MONEY32_UNDEFINED)
            {
                return std::nullopt;
            }
            cost += placementCost.value();
        }
    }

    return cost;
}

static std::optional<money32> track_design_place_maze(TrackDesign* td6, const CoordsXYZ& coords, Ride* ride)
{
    if (_trackDesignPlaceOperation == PTD_OPERATION_DRAW_OUTLINES)
    {
        gMapSelectionTiles.clear();
        gMapSelectArrowPosition = CoordsXYZ{ coords, tile_element_height(coords) };
        gMapSelectArrowDirection = _currentTrackPieceDirection;
    }

    _trackDesignPlaceZ = 0;
    money32 totalCost = 0;

    for (const auto& maze_element : td6->maze_elements)
    {
        uint8_t rotation = _currentTrackPieceDirection & 3;
        CoordsXY mazeMapPos = TileCoordsXY(maze_element.x, maze_element.y).ToCoordsXY();
        auto mapCoord = mazeMapPos.Rotate(rotation);
        mapCoord += coords;

        track_design_update_max_min_coordinates({ mapCoord, coords.z });

        if (_trackDesignPlaceOperation == PTD_OPERATION_DRAW_OUTLINES)
        {
            track_design_add_selection_tile(mapCoord);
        }

        if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY || _trackDesignPlaceOperation == PTD_OPERATION_PLACE
            || _trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST
            || _trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
        {
            uint8_t flags;
            money32 cost = 0;
            uint16_t maze_entry;
            switch (maze_element.type)
            {
                case MAZE_ELEMENT_TYPE_ENTRANCE:
                    // entrance
                    rotation += maze_element.direction;
                    rotation &= 3;

                    flags = GAME_COMMAND_FLAG_APPLY;
                    gGameCommandErrorTitle = STR_RIDE_CONSTRUCTION_CANT_CONSTRUCT_THIS_HERE;

                    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                    {
                        auto res = RideEntranceExitPlaceAction::TrackPlaceQuery({ mapCoord, coords.z }, false);
                        cost = res->Error == GameActions::Status::Ok ? res->Cost : MONEY32_UNDEFINED;
                    }
                    else
                    {
                        if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                        {
                            flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED
                                | GAME_COMMAND_FLAG_NO_SPEND;
                        }
                        else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                        {
                            flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
                                | GAME_COMMAND_FLAG_GHOST;
                        }
                        if (_trackDesignPlaceIsReplay)
                        {
                            flags |= GAME_COMMAND_FLAG_REPLAY;
                        }
                        auto rideEntranceExitPlaceAction = RideEntranceExitPlaceAction(mapCoord, rotation, ride->id, 0, false);
                        rideEntranceExitPlaceAction.SetFlags(flags);
                        auto res = GameActions::ExecuteNested(&rideEntranceExitPlaceAction);
                        cost = res->Error == GameActions::Status::Ok ? res->Cost : MONEY32_UNDEFINED;
                    }
                    if (cost != MONEY32_UNDEFINED)
                    {
                        _trackDesignPlaceStateEntranceExitPlaced = true;
                    }
                    break;
                case MAZE_ELEMENT_TYPE_EXIT:
                    // exit
                    rotation += maze_element.direction;
                    rotation &= 3;

                    flags = GAME_COMMAND_FLAG_APPLY;
                    gGameCommandErrorTitle = STR_RIDE_CONSTRUCTION_CANT_CONSTRUCT_THIS_HERE;

                    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                    {
                        auto res = RideEntranceExitPlaceAction::TrackPlaceQuery({ mapCoord, coords.z }, true);
                        cost = res->Error == GameActions::Status::Ok ? res->Cost : MONEY32_UNDEFINED;
                    }
                    else
                    {
                        if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                        {
                            flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED
                                | GAME_COMMAND_FLAG_NO_SPEND;
                        }
                        else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                        {
                            flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
                                | GAME_COMMAND_FLAG_GHOST;
                        }
                        if (_trackDesignPlaceIsReplay)
                        {
                            flags |= GAME_COMMAND_FLAG_REPLAY;
                        }
                        auto rideEntranceExitPlaceAction = RideEntranceExitPlaceAction(mapCoord, rotation, ride->id, 0, true);
                        rideEntranceExitPlaceAction.SetFlags(flags);
                        auto res = GameActions::ExecuteNested(&rideEntranceExitPlaceAction);
                        cost = res->Error == GameActions::Status::Ok ? res->Cost : MONEY32_UNDEFINED;
                    }
                    if (cost != MONEY32_UNDEFINED)
                    {
                        _trackDesignPlaceStateEntranceExitPlaced = true;
                    }
                    break;
                default:
                    maze_entry = Numerics::rol16(maze_element.maze_entry, rotation * 4);

                    if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                    {
                        flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND;
                    }
                    else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                    {
                        flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
                            | GAME_COMMAND_FLAG_GHOST;
                    }
                    else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                    {
                        flags = 0;
                    }
                    else
                    {
                        flags = GAME_COMMAND_FLAG_APPLY;
                    }
                    if (_trackDesignPlaceIsReplay)
                    {
                        flags |= GAME_COMMAND_FLAG_REPLAY;
                    }
                    gGameCommandErrorTitle = STR_RIDE_CONSTRUCTION_CANT_CONSTRUCT_THIS_HERE;

                    auto mazePlace = MazePlaceTrackAction({ mapCoord, coords.z }, ride->id, maze_entry);
                    mazePlace.SetFlags(flags);
                    auto res = flags & GAME_COMMAND_FLAG_APPLY ? GameActions::ExecuteNested(&mazePlace)
                                                               : GameActions::QueryNested(&mazePlace);
                    cost = res->Error == GameActions::Status::Ok ? res->Cost : MONEY32_UNDEFINED;
                    break;
            }

            totalCost += cost;

            if (cost == MONEY32_UNDEFINED)
            {
                return std::nullopt;
            }
        }

        if (_trackDesignPlaceOperation == PTD_OPERATION_GET_PLACE_Z)
        {
            if (!map_is_location_valid(mapCoord))
            {
                continue;
            }

            auto surfaceElement = map_get_surface_element_at(mapCoord);
            if (surfaceElement == nullptr)
                continue;
            int16_t surfaceZ = surfaceElement->GetBaseZ();
            if (surfaceElement->GetSlope() & TILE_ELEMENT_SLOPE_ALL_CORNERS_UP)
            {
                surfaceZ += LAND_HEIGHT_STEP;
                if (surfaceElement->GetSlope() & TILE_ELEMENT_SLOPE_DOUBLE_HEIGHT)
                {
                    surfaceZ += LAND_HEIGHT_STEP;
                }
            }

            int16_t waterZ = surfaceElement->GetWaterHeight();
            if (waterZ > 0 && waterZ > surfaceZ)
            {
                surfaceZ = waterZ;
            }

            int16_t temp_z = coords.z + _trackDesignPlaceZ - surfaceZ;
            if (temp_z < 0)
            {
                _trackDesignPlaceZ -= temp_z;
            }
        }
    }

    if (_trackDesignPlaceOperation == PTD_OPERATION_REMOVE_GHOST)
    {
        ride_action_modify(
            ride, RIDE_MODIFY_DEMOLISH,
            GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
                | GAME_COMMAND_FLAG_GHOST);
    }

    _trackPreviewOrigin = coords;
    return totalCost;
}

static std::optional<money32> track_design_place_ride(TrackDesign* td6, const CoordsXYZ& origin, Ride* ride)
{
    _trackPreviewOrigin = origin;
    if (_trackDesignPlaceOperation == PTD_OPERATION_DRAW_OUTLINES)
    {
        gMapSelectionTiles.clear();
        gMapSelectArrowPosition = CoordsXYZ{ origin, tile_element_height(origin) };
        gMapSelectArrowDirection = _currentTrackPieceDirection;
    }

    _trackDesignPlaceZ = 0;
    money32 totalCost = 0;
    uint8_t rotation = _currentTrackPieceDirection;

    // Track elements
    auto newCoords = origin;
    for (const auto& track : td6->track_elements)
    {
        auto trackType = track.type;
        const auto& ted = GetTrackElementDescriptor(trackType);

        track_design_update_max_min_coordinates(newCoords);

        switch (_trackDesignPlaceOperation)
        {
            case PTD_OPERATION_DRAW_OUTLINES:
                for (const rct_preview_track* trackBlock = ted.Block; trackBlock->index != 0xFF; trackBlock++)
                {
                    auto tile = CoordsXY{ newCoords } + CoordsXY{ trackBlock->x, trackBlock->y }.Rotate(rotation);
                    track_design_update_max_min_coordinates({ tile, newCoords.z });
                    track_design_add_selection_tile(tile);
                }
                break;
            case PTD_OPERATION_REMOVE_GHOST:
            {
                const rct_track_coordinates* trackCoordinates = &ted.Coordinates;
                const rct_preview_track* trackBlock = ted.Block;
                int32_t tempZ = newCoords.z - trackCoordinates->z_begin + trackBlock->z;
                auto trackRemoveAction = TrackRemoveAction(
                    trackType, 0, { newCoords, tempZ, static_cast<Direction>(rotation & 3) });
                trackRemoveAction.SetFlags(
                    GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND | GAME_COMMAND_FLAG_GHOST);
                GameActions::ExecuteNested(&trackRemoveAction);
                break;
            }
            case PTD_OPERATION_PLACE_QUERY:
            case PTD_OPERATION_PLACE:
            case PTD_OPERATION_PLACE_GHOST:
            case PTD_OPERATION_PLACE_TRACK_PREVIEW:
            {
                const rct_track_coordinates* trackCoordinates = &ted.Coordinates;

                // di
                int16_t tempZ = newCoords.z - trackCoordinates->z_begin;
                uint32_t trackColour = (track.flags >> 4) & 0x3;
                uint32_t brakeSpeed = (track.flags & 0x0F) * 2;
                uint32_t seatRotation = track.flags & 0x0F;

                int32_t liftHillAndAlternativeState = 0;
                if (track.flags & RCT12_TRACK_ELEMENT_TYPE_FLAG_CHAIN_LIFT)
                {
                    liftHillAndAlternativeState |= 1;
                }
                if (track.flags & TD6_TRACK_ELEMENT_FLAG_INVERTED)
                {
                    liftHillAndAlternativeState |= 2;
                }

                uint8_t flags = GAME_COMMAND_FLAG_APPLY;
                if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                {
                    flags |= GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED;
                    flags |= GAME_COMMAND_FLAG_NO_SPEND;
                }
                else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                {
                    flags |= GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED;
                    flags |= GAME_COMMAND_FLAG_NO_SPEND;
                    flags |= GAME_COMMAND_FLAG_GHOST;
                }
                else if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                {
                    flags = GAME_COMMAND_FLAG_NO_SPEND;
                }
                if (_trackDesignPlaceIsReplay)
                {
                    flags |= GAME_COMMAND_FLAG_REPLAY;
                }
                gGameCommandErrorTitle = STR_RIDE_CONSTRUCTION_CANT_CONSTRUCT_THIS_HERE;
                auto trackPlaceAction = TrackPlaceAction(
                    _currentRideIndex, trackType, { newCoords, tempZ, static_cast<uint8_t>(rotation) }, brakeSpeed, trackColour,
                    seatRotation, liftHillAndAlternativeState, true);
                trackPlaceAction.SetFlags(flags);

                auto res = flags & GAME_COMMAND_FLAG_APPLY ? GameActions::ExecuteNested(&trackPlaceAction)
                                                           : GameActions::QueryNested(&trackPlaceAction);
                money32 cost = res->Error == GameActions::Status::Ok ? res->Cost : MONEY32_UNDEFINED;

                totalCost += cost;
                if (cost == MONEY32_UNDEFINED)
                {
                    return std::nullopt;
                }
                break;
            }
            case PTD_OPERATION_GET_PLACE_Z:
            {
                int32_t tempZ = newCoords.z - ted.Coordinates.z_begin;
                for (const rct_preview_track* trackBlock = ted.Block; trackBlock->index != 0xFF; trackBlock++)
                {
                    auto tile = CoordsXY{ newCoords } + CoordsXY{ trackBlock->x, trackBlock->y }.Rotate(rotation);
                    if (!map_is_location_valid(tile))
                    {
                        continue;
                    }

                    auto surfaceElement = map_get_surface_element_at(tile);
                    if (surfaceElement == nullptr)
                    {
                        return std::nullopt;
                    }

                    int32_t surfaceZ = surfaceElement->GetBaseZ();
                    if (surfaceElement->GetSlope() & TILE_ELEMENT_SLOPE_ALL_CORNERS_UP)
                    {
                        surfaceZ += LAND_HEIGHT_STEP;
                        if (surfaceElement->GetSlope() & TILE_ELEMENT_SLOPE_DOUBLE_HEIGHT)
                        {
                            surfaceZ += LAND_HEIGHT_STEP;
                        }
                    }

                    auto waterZ = surfaceElement->GetWaterHeight();
                    if (waterZ > 0 && waterZ > surfaceZ)
                    {
                        surfaceZ = waterZ;
                    }
                    int32_t heightDifference = tempZ + _trackDesignPlaceZ + trackBlock->z - surfaceZ;
                    if (heightDifference < 0)
                    {
                        _trackDesignPlaceZ -= heightDifference;
                    }
                }
                break;
            }
        }

        const rct_track_coordinates& track_coordinates = ted.Coordinates;
        auto offsetAndRotatedTrack = CoordsXY{ newCoords }
            + CoordsXY{ track_coordinates.x, track_coordinates.y }.Rotate(rotation);

        newCoords = { offsetAndRotatedTrack, newCoords.z - track_coordinates.z_begin + track_coordinates.z_end };
        rotation = (rotation + track_coordinates.rotation_end - track_coordinates.rotation_begin) & 3;
        if (track_coordinates.rotation_end & (1 << 2))
        {
            rotation |= (1 << 2);
        }
        else
        {
            newCoords += CoordsDirectionDelta[rotation];
        }
    }

    // Entrance elements
    for (const auto& entrance : td6->entrance_elements)
    {
        rotation = _currentTrackPieceDirection & 3;
        CoordsXY entranceMapPos{ entrance.x, entrance.y };
        auto rotatedEntranceMapPos = entranceMapPos.Rotate(rotation);
        newCoords = { rotatedEntranceMapPos + _trackPreviewOrigin, newCoords.z };

        track_design_update_max_min_coordinates(newCoords);

        switch (_trackDesignPlaceOperation)
        {
            case PTD_OPERATION_DRAW_OUTLINES:
                track_design_add_selection_tile(newCoords);
                break;
            case PTD_OPERATION_PLACE_QUERY:
            case PTD_OPERATION_PLACE:
            case PTD_OPERATION_PLACE_GHOST:
            case PTD_OPERATION_PLACE_TRACK_PREVIEW:
            {
                rotation = (rotation + entrance.direction) & 3;
                if (_trackDesignPlaceOperation != PTD_OPERATION_PLACE_QUERY)
                {
                    auto tile = CoordsXY{ newCoords } + CoordsDirectionDelta[rotation];
                    TileElement* tile_element = map_get_first_element_at(tile);
                    newCoords.z = _trackPreviewOrigin.z / COORDS_Z_STEP;
                    newCoords.z += entrance.z;
                    if (tile_element == nullptr)
                    {
                        return std::nullopt;
                    }

                    do
                    {
                        if (tile_element->GetType() != TILE_ELEMENT_TYPE_TRACK)
                        {
                            continue;
                        }
                        if (tile_element->base_height != newCoords.z)
                        {
                            continue;
                        }

                        auto stationIndex = tile_element->AsTrack()->GetStationIndex();
                        uint8_t flags = GAME_COMMAND_FLAG_APPLY;
                        if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_TRACK_PREVIEW)
                        {
                            flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED
                                | GAME_COMMAND_FLAG_NO_SPEND;
                        }
                        if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_GHOST)
                        {
                            flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
                                | GAME_COMMAND_FLAG_GHOST;
                        }
                        if (_trackDesignPlaceOperation == PTD_OPERATION_PLACE_QUERY)
                        {
                            flags = 0;
                        }
                        if (_trackDesignPlaceIsReplay)
                        {
                            flags |= GAME_COMMAND_FLAG_REPLAY;
                        }
                        gGameCommandErrorTitle = STR_RIDE_CONSTRUCTION_CANT_CONSTRUCT_THIS_HERE;
                        auto rideEntranceExitPlaceAction = RideEntranceExitPlaceAction(
                            newCoords, rotation, ride->id, stationIndex, entrance.isExit);
                        rideEntranceExitPlaceAction.SetFlags(flags);
                        auto res = flags & GAME_COMMAND_FLAG_APPLY ? GameActions::ExecuteNested(&rideEntranceExitPlaceAction)
                                                                   : GameActions::QueryNested(&rideEntranceExitPlaceAction);

                        totalCost += res->Cost;

                        if (res->Error != GameActions::Status::Ok)
                        {
                            return std::nullopt;
                        }
                        _trackDesignPlaceStateEntranceExitPlaced = true;
                        break;
                    } while (!(tile_element++)->IsLastForTile());
                }
                else
                {
                    newCoords.z = entrance.z * COORDS_Z_STEP;
                    newCoords.z += _trackPreviewOrigin.z;

                    auto res = RideEntranceExitPlaceAction::TrackPlaceQuery(newCoords, false);
                    if (res->Error != GameActions::Status::Ok)
                    {
                        return std::nullopt;
                    }

                    totalCost += res->Cost;
                    _trackDesignPlaceStateEntranceExitPlaced = true;
                }
                break;
            }
        }
    }

    if (_trackDesignPlaceOperation == PTD_OPERATION_REMOVE_GHOST)
    {
        ride->ValidateStations();
        ride->Delete();
    }
    return totalCost;
}

/**
 * Places a virtual track. This can involve highlighting the surface tiles and showing the track layout. It is also used by
 * the track preview window to place the whole track.
 * Depending on the value of bl it modifies the function.
 * bl == 0, Draw outlines on the ground
 * bl == 1,
 * bl == 2,
 * bl == 3, Returns the z value of a successful placement. Only lower 16 bits are the value, the rest may be garbage?
 * bl == 4,
 * bl == 5, Returns cost to create the track. All 32 bits are used. Places the track. (used by the preview)
 * bl == 6, Clear white outlined track.
 *  rct2: 0x006D01B3
 */
money32 place_virtual_track(TrackDesign* td6, uint8_t ptdOperation, bool placeScenery, Ride* ride, const CoordsXYZ& coords)
{
    // Previously byte_F4414E was cleared here
    _trackDesignPlaceStatePlaceScenery = placeScenery;
    _trackDesignPlaceStateEntranceExitPlaced = false;
    _trackDesignPlaceStateSceneryUnavailable = false;
    _trackDesignPlaceStateHasScenery = false;

    _trackDesignPlaceIsReplay = ptdOperation & PTD_OPERATION_FLAG_IS_REPLAY;
    ptdOperation &= ~PTD_OPERATION_FLAG_IS_REPLAY;
    _trackDesignPlaceOperation = ptdOperation;
    if (gTrackDesignSceneryToggle)
    {
        _trackDesignPlaceStatePlaceScenery = false;
    }
    _currentRideIndex = ride->id;

    _trackPreviewMin = coords;
    _trackPreviewMax = coords;

    _trackDesignPlaceSceneryZ = 0;
    std::optional<money32> trackPlaceCost;
    if (td6->type == RIDE_TYPE_MAZE)
    {
        trackPlaceCost = track_design_place_maze(td6, coords, ride);
    }
    else
    {
        trackPlaceCost = track_design_place_ride(td6, coords, ride);
    }

    // Scenery elements
    if (trackPlaceCost.has_value())
    {
        auto sceneryCost = track_design_place_all_scenery(td6->scenery_elements, _trackPreviewOrigin);
        if (!sceneryCost.has_value())
        {
            return MONEY32_UNDEFINED;
        }

        trackPlaceCost = trackPlaceCost.value() + sceneryCost.value();
    }

    // 0x6D0FE6
    if (_trackDesignPlaceOperation == PTD_OPERATION_DRAW_OUTLINES)
    {
        gMapSelectFlags |= MAP_SELECT_FLAG_ENABLE_CONSTRUCT;
        gMapSelectFlags |= MAP_SELECT_FLAG_ENABLE_ARROW;
        gMapSelectFlags &= ~MAP_SELECT_FLAG_GREEN;
        map_invalidate_map_selection_tiles();
    }

    if (ptdOperation == PTD_OPERATION_GET_PLACE_Z)
    {
        // Change from vanilla: originally, _trackDesignPlaceSceneryZ was not subtracted
        // from _trackDesignPlaceZ, causing bug #259.
        return _trackDesignPlaceZ - _trackDesignPlaceSceneryZ;
    }

    return trackPlaceCost.has_value() ? trackPlaceCost.value() : MONEY32_UNDEFINED;
}

static money32 track_design_ride_create_command(int32_t type, int32_t subType, int32_t flags, ride_id_t* outRideIndex)
{
    // Don't set colours as will be set correctly later.
    auto gameAction = RideCreateAction(type, subType, 0, 0);
    gameAction.SetFlags(flags);

    auto res = GameActions::ExecuteNested(&gameAction);

    // Callee's of this function expect MONEY32_UNDEFINED in case of failure.
    if (res->Error != GameActions::Status::Ok)
    {
        return MONEY32_UNDEFINED;
    }

    *outRideIndex = res->GetData<ride_id_t>();

    return res->Cost;
}

/**
 *
 *  rct2: 0x006D2189
 * ebx = ride_id
 * cost = edi
 */
static bool track_design_place_preview(TrackDesign* td6, money32* cost, Ride** outRide, uint8_t* flags)
{
    *outRide = nullptr;
    *flags = 0;

    auto& objManager = GetContext()->GetObjectManager();
    auto entry_index = objManager.GetLoadedObjectEntryIndex(td6->vehicle_object);

    ride_id_t rideIndex;
    uint8_t rideCreateFlags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND;
    if (track_design_ride_create_command(td6->type, entry_index, rideCreateFlags, &rideIndex) == MONEY32_UNDEFINED)
    {
        return false;
    }

    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return false;

    ride->custom_name = {};

    auto stationIdentifier = GetStationIdentifierFromStyle(td6->entrance_style);
    ride->entrance_style = objManager.GetLoadedObjectEntryIndex(stationIdentifier);
    if (ride->entrance_style == OBJECT_ENTRY_INDEX_NULL)
    {
        ride->entrance_style = gLastEntranceStyle;
    }

    for (int32_t i = 0; i < RCT12_NUM_COLOUR_SCHEMES; i++)
    {
        ride->track_colour[i].main = td6->track_spine_colour[i];
        ride->track_colour[i].additional = td6->track_rail_colour[i];
        ride->track_colour[i].supports = td6->track_support_colour[i];
    }

    // Flat rides need their vehicle colours loaded for display
    // in the preview window
    if (!GetRideTypeDescriptor(td6->type).HasFlag(RIDE_TYPE_FLAG_HAS_TRACK))
    {
        for (int32_t i = 0; i < RCT12_MAX_VEHICLE_COLOURS; i++)
        {
            ride->vehicle_colours[i].Body = td6->vehicle_colours[i].body_colour;
            ride->vehicle_colours[i].Trim = td6->vehicle_colours[i].trim_colour;
            ride->vehicle_colours[i].Ternary = td6->vehicle_additional_colour[i];
        }
    }

    _trackDesignDrawingPreview = true;
    uint8_t backup_rotation = _currentTrackPieceDirection;
    uint32_t backup_park_flags = gParkFlags;
    gParkFlags &= ~PARK_FLAGS_FORBID_HIGH_CONSTRUCTION;
    int32_t mapSize = gMapSize << 4;

    _currentTrackPieceDirection = 0;
    int32_t z = place_virtual_track(
        td6, PTD_OPERATION_GET_PLACE_Z, true, GetOrAllocateRide(PreviewRideId), { mapSize, mapSize, 16 });

    if (_trackDesignPlaceStateHasScenery)
    {
        *flags |= TRACK_DESIGN_FLAG_HAS_SCENERY;
    }

    z += 16 - _trackDesignPlaceSceneryZ;

    bool placeScenery = true;
    if (_trackDesignPlaceStateSceneryUnavailable)
    {
        placeScenery = false;
        *flags |= TRACK_DESIGN_FLAG_SCENERY_UNAVAILABLE;
    }

    money32 resultCost = place_virtual_track(
        td6, PTD_OPERATION_PLACE_TRACK_PREVIEW, placeScenery, ride, { mapSize, mapSize, z });
    gParkFlags = backup_park_flags;

    if (resultCost != MONEY32_UNDEFINED)
    {
        if (entry_index == OBJECT_ENTRY_INDEX_NULL)
        {
            *flags |= TRACK_DESIGN_FLAG_VEHICLE_UNAVAILABLE;
        }
        else if (!ride_entry_is_invented(entry_index) && !gCheatsIgnoreResearchStatus)
        {
            *flags |= TRACK_DESIGN_FLAG_VEHICLE_UNAVAILABLE;
        }

        _currentTrackPieceDirection = backup_rotation;
        _trackDesignDrawingPreview = false;
        *cost = resultCost;
        *outRide = ride;
        return true;
    }

    _currentTrackPieceDirection = backup_rotation;
    ride->Delete();
    _trackDesignDrawingPreview = false;
    return false;
}

#pragma region Track Design Preview

/**
 *
 *  rct2: 0x006D1EF0
 */
void track_design_draw_preview(TrackDesign* td6, uint8_t* pixels)
{
    StashMap();
    track_design_preview_clear_map();

    if (gScreenFlags & SCREEN_FLAGS_TRACK_MANAGER)
    {
        track_design_load_scenery_objects(td6);
    }

    money32 cost;
    Ride* ride;
    uint8_t flags;
    if (!track_design_place_preview(td6, &cost, &ride, &flags))
    {
        std::fill_n(pixels, TRACK_PREVIEW_IMAGE_SIZE * 4, 0x00);
        UnstashMap();
        return;
    }
    td6->cost = cost;
    td6->track_flags = flags & 7;

    CoordsXYZ centre = { (_trackPreviewMin.x + _trackPreviewMax.x) / 2 + 16, (_trackPreviewMin.y + _trackPreviewMax.y) / 2 + 16,
                         (_trackPreviewMin.z + _trackPreviewMax.z) / 2 };

    int32_t size_x = _trackPreviewMax.x - _trackPreviewMin.x;
    int32_t size_y = _trackPreviewMax.y - _trackPreviewMin.y;
    int32_t size_z = _trackPreviewMax.z - _trackPreviewMin.z;

    // Special case for flat rides - Z-axis info is irrelevant
    // and must be zeroed out lest the preview be off-centre
    if (!GetRideTypeDescriptor(td6->type).HasFlag(RIDE_TYPE_FLAG_HAS_TRACK))
    {
        centre.z = 0;
        size_z = 0;
    }

    int32_t zoom_level = 1;

    if (size_x < size_y)
    {
        size_x = size_y;
    }

    if (size_x > 1000 || size_z > 280)
    {
        zoom_level = 2;
    }

    if (size_x > 1600 || size_z > 1000)
    {
        zoom_level = 3;
    }

    size_x = 370 << zoom_level;
    size_y = 217 << zoom_level;

    rct_viewport view;
    view.width = 370;
    view.height = 217;
    view.view_width = size_x;
    view.view_height = size_y;
    view.pos = { 0, 0 };
    view.zoom = zoom_level;
    view.flags = VIEWPORT_FLAG_HIDE_BASE | VIEWPORT_FLAG_INVISIBLE_SPRITES;

    rct_drawpixelinfo dpi;
    dpi.zoom_level = zoom_level;
    dpi.x = 0;
    dpi.y = 0;
    dpi.width = 370;
    dpi.height = 217;
    dpi.pitch = 0;
    dpi.bits = pixels;

    auto drawingEngine = std::make_unique<X8DrawingEngine>(GetContext()->GetUiContext());
    dpi.DrawingEngine = drawingEngine.get();

    const ScreenCoordsXY offset = { size_x / 2, size_y / 2 };
    for (uint8_t i = 0; i < 4; i++)
    {
        gCurrentRotation = i;

        view.viewPos = translate_3d_to_2d_with_z(i, centre) - offset;
        viewport_paint(&view, &dpi, { view.viewPos, view.viewPos + ScreenCoordsXY{ size_x, size_y } });

        dpi.bits += TRACK_PREVIEW_IMAGE_SIZE;
    }

    ride->Delete();
    UnstashMap();
}

/**
 * Resets all the map elements to surface tiles for track preview.
 *  rct2: 0x006D1D9A
 */
static void track_design_preview_clear_map()
{
    auto numTiles = MAXIMUM_MAP_SIZE_TECHNICAL * MAXIMUM_MAP_SIZE_TECHNICAL;

    gMapSize = 256;

    // Reserve ~8 elements per tile
    std::vector<TileElement> tileElements;
    tileElements.reserve(numTiles * 8);

    for (int32_t i = 0; i < numTiles; i++)
    {
        auto* element = &tileElements.emplace_back();
        element->ClearAs(TILE_ELEMENT_TYPE_SURFACE);
        element->SetLastForTile(true);
        element->AsSurface()->SetSlope(TILE_ELEMENT_SLOPE_FLAT);
        element->AsSurface()->SetWaterHeight(0);
        element->AsSurface()->SetSurfaceStyle(0);
        element->AsSurface()->SetEdgeStyle(0);
        element->AsSurface()->SetGrassLength(GRASS_LENGTH_CLEAR_0);
        element->AsSurface()->SetOwnership(OWNERSHIP_OWNED);
        element->AsSurface()->SetParkFences(0);
    }
    SetTileElements(std::move(tileElements));
}

bool track_design_are_entrance_and_exit_placed()
{
    return _trackDesignPlaceStateEntranceExitPlaced;
}

#pragma endregion