#ifndef I18N_H
#define I18N_H

enum {
    S_WELCOME = 0,
    S_WELCOME_SUB,
    S_LANGUAGE,
    S_LANGUAGE_SUB,
    S_TIMEZONE,
    S_TIMEZONE_SUB,
    S_CREATE_USER,
    S_CREATE_USER_SUB,
    S_DONE,
    S_DONE_SUB,
    S_NEXT,
    S_BACK,
    S_SKIP,
    S_START,
    S_USERNAME,
    S_PASSWORD,
    S_SIGN_IN,
    S_LOGIN_FAILED,
    S_GUEST_HINT,
    S_USER_LABEL,
    S_GUEST,
    S_LANG_EN,
    S_LANG_ZH,
    S_SUMMARY_LANG,
    S_SUMMARY_TZ,
    S_SUMMARY_USER,
    S_NO_USER,
    S_CURRENT_TIME,
    S_ERR_EMPTY,
    S_ERR_INVALID,
    S_ERR_EXISTS,
    S_ERR_FULL,
    S_ERR_FAIL,
    S_ICON_FILES,
    S_ICON_NOTEPAD,
    S_ICON_ABOUT,
    S_ICON_SYSTEM,
    S_ICON_TERMINAL,
    S_ICON_REBOOT,
    S_ICON_SHUTDOWN,
    S_COUNT
};

#define LANG_EN 0
#define LANG_ZH 1

void i18n_set_lang(int lang);
int i18n_lang(void);
const char* tr(int id);

#endif
