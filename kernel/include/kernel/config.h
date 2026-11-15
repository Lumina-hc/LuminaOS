#ifndef CONFIG_H
#define CONFIG_H

void config_reset(void);
int config_load(void);
int config_save(void);
int config_done(void);
int config_lang(void);
int config_tz(void);
void config_set_lang(int lang);
void config_set_tz(int minutes);
void config_mark_done(void);

#endif
