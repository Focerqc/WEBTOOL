#ifndef WEBFSBACKUPBRIDGE_H
#define WEBFSBACKUPBRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*WebFsFolderCallback)(int success, const char *folderName, void *userData);
typedef void (*WebFsActionCallback)(int success, const char *message, void *userData);
typedef void (*WebFsListCallback)(int success, const char *jsonList, void *userData);
typedef void (*WebFsReadCallback)(int success, const char *jsonData, void *userData);

/**
 * @brief Initialize the File System Access API bridge and attempt to reload saved directory handle from IndexedDB.
 */
void webfs_init(void);

/**
 * @brief Returns 1 if File System Access API (showDirectoryPicker) is supported in this browser, 0 otherwise.
 */
int webfs_is_supported(void);

/**
 * @brief Returns the name of the currently active/saved directory, or empty string if none.
 */
const char *webfs_get_saved_folder_name(void);

/**
 * @brief Prompt user with showDirectoryPicker() and persist handle to IndexedDB.
 */
void webfs_select_folder(WebFsFolderCallback callback, void *userData);

/**
 * @brief Save a set of files (mcconf.xml, appconf.xml, info.json, etc.) into a subfolder inside the selected directory.
 * @param subfolderName Name of the subfolder to create (e.g. "Thor_CAN3_2026-09-18_06-30-00")
 * @param filesJson JSON object where keys are filenames and values are string contents.
 */
void webfs_save_backup(const char *subfolderName, const char *filesJson, WebFsActionCallback callback, void *userData);

/**
 * @brief Query the selected directory for previous backup subfolders and return a JSON list.
 */
void webfs_list_backups(WebFsListCallback callback, void *userData);

/**
 * @brief Read configuration files from a specific backup subfolder.
 */
void webfs_read_backup(const char *subfolderName, WebFsReadCallback callback, void *userData);

#ifdef __cplusplus
}
#endif

#endif // WEBFSBACKUPBRIDGE_H
