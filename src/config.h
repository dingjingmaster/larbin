/*************************************************************************
> FileName: config.h
> Author  : DingJing
> Mail    : dingjing@live.cn
> Created Time: 2018年01月30日 星期二 15时04分55秒
 ************************************************************************/
#ifndef _CONFIG_H
#define _CONFIG_H
/**
 * 负责配置文件的读取与解析
 *
 */

class Config {
public:
    Config ();
    ~Config ();
    void getConffName();
    void setConffName(string name);
    void parse();

private:
    string cfname;
}



#endif
