/*
 * $Id$
 */

#ifndef _APPINFO_H
#define _APPINFO_H

/* Name of the XML file where the info is stored */
#define APPINFO_FILENAME		"AppInfo.xml"

/* External interface */
XMLwrapper *appinfo_get(const gchar *app_dir, DirItem *item);
void appinfo_unref(XMLwrapper *info);
xmlNode *appinfo_get_section(XMLwrapper *ai, const gchar *name);

#endif   /* _APPINFO_H */
