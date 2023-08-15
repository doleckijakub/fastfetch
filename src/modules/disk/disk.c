#include "common/printing.h"
#include "common/jsonconfig.h"
#include "common/parsing.h"
#include "common/bar.h"
#include "detection/disk/disk.h"
#include "modules/disk/disk.h"
#include "util/stringUtils.h"

#define FF_DISK_NUM_FORMAT_ARGS 10
#pragma GCC diagnostic ignored "-Wsign-conversion"

static void printDisk(FFDiskOptions* options, const FFDisk* disk)
{
    FF_STRBUF_AUTO_DESTROY key = ffStrbufCreate();

    if(options->moduleArgs.key.length == 0)
    {
        if(instance.config.pipe)
            ffStrbufAppendF(&key, "%s (%s)", FF_DISK_MODULE_NAME, disk->mountpoint.chars);
        else
        {
            #ifdef __linux__
            if (getenv("WSL_DISTRO_NAME") != NULL && getenv("WT_SESSION") != NULL)
            {
                if (ffStrbufEqualS(&disk->filesystem, "9p") && ffStrbufStartsWithS(&disk->mountpoint, "/mnt/"))
                    ffStrbufAppendF(&key, "%s (\e]8;;file:///%c:/\e\\%s\e]8;;\e\\)", FF_DISK_MODULE_NAME, disk->mountpoint.chars[5], disk->mountpoint.chars);
                else
                    ffStrbufAppendF(&key, "%s (\e]8;;file:////wsl.localhost/%s%s\e\\%s\e]8;;\e\\)", FF_DISK_MODULE_NAME, getenv("WSL_DISTRO_NAME"), disk->mountpoint.chars, disk->mountpoint.chars);
            }
            else
            #endif
            ffStrbufAppendF(&key, "%s (\e]8;;file://%s\e\\%s\e]8;;\e\\)", FF_DISK_MODULE_NAME, disk->mountpoint.chars, disk->mountpoint.chars);
        }
    }
    else
    {
        ffParseFormatString(&key, &options->moduleArgs.key, 2, (FFformatarg[]){
            {FF_FORMAT_ARG_TYPE_STRBUF, &disk->mountpoint},
            {FF_FORMAT_ARG_TYPE_STRBUF, &disk->name},
        });
    }

    FF_STRBUF_AUTO_DESTROY usedPretty = ffStrbufCreate();
    ffParseSize(disk->bytesUsed, &usedPretty);

    FF_STRBUF_AUTO_DESTROY totalPretty = ffStrbufCreate();
    ffParseSize(disk->bytesTotal, &totalPretty);

    uint8_t bytesPercentage = disk->bytesTotal > 0 ? (uint8_t) (((long double) disk->bytesUsed / (long double) disk->bytesTotal) * 100.0) : 0;

    if(options->moduleArgs.outputFormat.length == 0)
    {
        ffPrintLogoAndKey(key.chars, 0, NULL, &options->moduleArgs.keyColor);

        FF_STRBUF_AUTO_DESTROY str = ffStrbufCreate();

        if(disk->bytesTotal > 0)
        {
            if(instance.config.percentType & FF_PERCENTAGE_TYPE_BAR_BIT)
            {
                ffAppendPercentBar(&str, bytesPercentage, 0, 5, 8);
                ffStrbufAppendC(&str, ' ');
            }

            if(!(instance.config.percentType & FF_PERCENTAGE_TYPE_HIDE_OTHERS_BIT))
                ffStrbufAppendF(&str, "%s / %s ", usedPretty.chars, totalPretty.chars);

            if(instance.config.percentType & FF_PERCENTAGE_TYPE_NUM_BIT)
            {
                ffAppendPercentNum(&str, (uint8_t) bytesPercentage, 50, 80, str.length > 0);
                ffStrbufAppendC(&str, ' ');
            }
        }
        else
            ffStrbufAppendS(&str, "Unknown ");

        if(!(instance.config.percentType & FF_PERCENTAGE_TYPE_HIDE_OTHERS_BIT))
        {
            if(disk->filesystem.length)
                ffStrbufAppendF(&str, "- %s ", disk->filesystem.chars);

            if(disk->type & FF_DISK_TYPE_EXTERNAL_BIT)
                ffStrbufAppendS(&str, "[External]");
            else if(disk->type & FF_DISK_TYPE_SUBVOLUME_BIT)
                ffStrbufAppendS(&str, "[Subvolume]");
            else if(disk->type & FF_DISK_TYPE_HIDDEN_BIT)
                ffStrbufAppendS(&str, "[Hidden]");
        }

        ffStrbufTrimRight(&str, ' ');
        ffStrbufPutTo(&str, stdout);
    }
    else
    {
        uint8_t filesPercentage = disk->filesTotal > 0 ? (uint8_t) (((double) disk->filesUsed / (double) disk->filesTotal) * 100.0) : 0;

        bool isExternal = !!(disk->type & FF_DISK_TYPE_EXTERNAL_BIT);
        bool isHidden = !!(disk->type & FF_DISK_TYPE_HIDDEN_BIT);
        ffPrintFormatString(key.chars, 0, NULL, &options->moduleArgs.keyColor, &options->moduleArgs.outputFormat, FF_DISK_NUM_FORMAT_ARGS, (FFformatarg[]){
            {FF_FORMAT_ARG_TYPE_STRBUF, &usedPretty},
            {FF_FORMAT_ARG_TYPE_STRBUF, &totalPretty},
            {FF_FORMAT_ARG_TYPE_UINT8, &bytesPercentage},
            {FF_FORMAT_ARG_TYPE_UINT, &disk->filesUsed},
            {FF_FORMAT_ARG_TYPE_UINT, &disk->filesTotal},
            {FF_FORMAT_ARG_TYPE_UINT8, &filesPercentage},
            {FF_FORMAT_ARG_TYPE_BOOL, &isExternal},
            {FF_FORMAT_ARG_TYPE_BOOL, &isHidden},
            {FF_FORMAT_ARG_TYPE_STRBUF, &disk->filesystem},
            {FF_FORMAT_ARG_TYPE_STRBUF, &disk->name}
        });
    }
}

static void printMountpoint(FFDiskOptions* options, const FFlist* disks, const char* mountpoint)
{
    FF_LIST_FOR_EACH(FFDisk, disk, *disks)
    {
        if(ffStrbufEqualS(&disk->mountpoint, mountpoint))
        {
            printDisk(options, disk);
            return;
        }
    }

    ffPrintError(FF_DISK_MODULE_NAME, 0, &options->moduleArgs, "No disk found for mountpoint: %s", mountpoint);
}

static void printMountpoints(FFDiskOptions* options, const FFlist* disks)
{
    #ifdef _WIN32
    const char separator = ';';
    #else
    const char separator = ':';
    #endif

    FF_STRBUF_AUTO_DESTROY mountpoints = ffStrbufCreateCopy(&options->folders);
    ffStrbufTrim(&mountpoints, separator);

    uint32_t startIndex = 0;
    while(startIndex < mountpoints.length)
    {
        uint32_t colonIndex = ffStrbufNextIndexC(&mountpoints, startIndex, separator);
        mountpoints.chars[colonIndex] = '\0';

        printMountpoint(options, disks, mountpoints.chars + startIndex);

        startIndex = colonIndex + 1;
    }
}

static void printAutodetected(FFDiskOptions* options, const FFlist* disks)
{
    FF_LIST_FOR_EACH(FFDisk, disk, *disks)
    {
        if(!(disk->type & options->showTypes))
            continue;

        printDisk(options, disk);
    }
}

void ffPrintDisk(FFDiskOptions* options)
{
    FF_LIST_AUTO_DESTROY disks = ffListCreate(sizeof (FFDisk));
    const char* error = ffDetectDisks(&disks);

    if(error)
    {
        ffPrintError(FF_DISK_MODULE_NAME, 0, &options->moduleArgs, "%s", error);
    }
    else
    {
        if(options->folders.length == 0)
            printAutodetected(options, &disks);
        else
            printMountpoints(options, &disks);
    }

    FF_LIST_FOR_EACH(FFDisk, disk, disks)
    {
        ffStrbufDestroy(&disk->mountpoint);
        ffStrbufDestroy(&disk->filesystem);
        ffStrbufDestroy(&disk->name);
    }
}


void ffInitDiskOptions(FFDiskOptions* options)
{
    options->moduleName = FF_DISK_MODULE_NAME;
    ffOptionInitModuleArg(&options->moduleArgs);

    ffStrbufInit(&options->folders);
    options->showTypes = FF_DISK_TYPE_REGULAR_BIT | FF_DISK_TYPE_EXTERNAL_BIT;
}

bool ffParseDiskCommandOptions(FFDiskOptions* options, const char* key, const char* value)
{
    const char* subKey = ffOptionTestPrefix(key, FF_DISK_MODULE_NAME);
    if (!subKey) return false;
    if (ffOptionParseModuleArgs(key, subKey, value, &options->moduleArgs))
        return true;

    if (ffStrEqualsIgnCase(subKey, "folders"))
    {
        ffOptionParseString(key, value, &options->folders);
        return true;
    }

    if (ffStrEqualsIgnCase(subKey, "show-regular"))
    {
        if (ffOptionParseBoolean(value))
            options->showTypes |= FF_DISK_TYPE_REGULAR_BIT;
        else
            options->showTypes &= ~FF_DISK_TYPE_REGULAR_BIT;
        return true;
    }

    if (ffStrEqualsIgnCase(subKey, "show-external"))
    {
        if (ffOptionParseBoolean(value))
            options->showTypes |= FF_DISK_TYPE_EXTERNAL_BIT;
        else
            options->showTypes &= ~FF_DISK_TYPE_EXTERNAL_BIT;
        return true;
    }

    if (ffStrEqualsIgnCase(subKey, "show-hidden"))
    {
        if (ffOptionParseBoolean(value))
            options->showTypes |= FF_DISK_TYPE_HIDDEN_BIT;
        else
            options->showTypes &= ~FF_DISK_TYPE_HIDDEN_BIT;
        return true;
    }

    if (ffStrEqualsIgnCase(subKey, "show-subvolumes"))
    {
        if (ffOptionParseBoolean(value))
            options->showTypes |= FF_DISK_TYPE_SUBVOLUME_BIT;
        else
            options->showTypes &= ~FF_DISK_TYPE_SUBVOLUME_BIT;
        return true;
    }

    if (ffStrEqualsIgnCase(subKey, "show-unknown"))
    {
        if (ffOptionParseBoolean(value))
            options->showTypes |= FF_DISK_TYPE_UNKNOWN_BIT;
        else
            options->showTypes &= ~FF_DISK_TYPE_UNKNOWN_BIT;
        return true;
    }

    return false;
}

void ffDestroyDiskOptions(FFDiskOptions* options)
{
    ffOptionDestroyModuleArg(&options->moduleArgs);
}

void ffParseDiskJsonObject(yyjson_val* module)
{
    FFDiskOptions __attribute__((__cleanup__(ffDestroyDiskOptions))) options;
    ffInitDiskOptions(&options);

    if (module)
    {
        yyjson_val *key_, *val;
        size_t idx, max;
        yyjson_obj_foreach(module, idx, max, key_, val)
        {
            const char* key = yyjson_get_str(key_);
            if(ffStrEqualsIgnCase(key, "type"))
                continue;

            if (ffJsonConfigParseModuleArgs(key, val, &options.moduleArgs))
                continue;

            if (ffStrEqualsIgnCase(key, "folders"))
            {
                ffStrbufSetS(&options.folders, yyjson_get_str(val));
                continue;
            }

            if (ffStrEqualsIgnCase(key, "showExternal"))
            {
                if (yyjson_get_bool(val))
                    options.showTypes |= FF_DISK_TYPE_EXTERNAL_BIT;
                else
                    options.showTypes &= ~FF_DISK_TYPE_EXTERNAL_BIT;
                continue;
            }

            if (ffStrEqualsIgnCase(key, "showHidden"))
            {
                if (yyjson_get_bool(val))
                    options.showTypes |= FF_DISK_TYPE_HIDDEN_BIT;
                else
                    options.showTypes &= ~FF_DISK_TYPE_HIDDEN_BIT;
                continue;
            }

            if (ffStrEqualsIgnCase(key, "showSubvolumes"))
            {
                if (yyjson_get_bool(val))
                    options.showTypes |= FF_DISK_TYPE_SUBVOLUME_BIT;
                else
                    options.showTypes &= ~FF_DISK_TYPE_SUBVOLUME_BIT;
                continue;
            }

            if (ffStrEqualsIgnCase(key, "showUnknown"))
            {
                if (yyjson_get_bool(val))
                    options.showTypes |= FF_DISK_TYPE_UNKNOWN_BIT;
                else
                    options.showTypes &= ~FF_DISK_TYPE_UNKNOWN_BIT;
                continue;
            }

            ffPrintError(FF_DISK_MODULE_NAME, 0, &options.moduleArgs, "Unknown JSON key %s", key);
        }
    }

    ffPrintDisk(&options);
}
