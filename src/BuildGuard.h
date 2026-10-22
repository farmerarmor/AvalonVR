#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#pragma comment(lib,"bcrypt.lib")
inline std::string SHA256(const std::filesystem::path& path){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};DWORD size{},written{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&size,sizeof(size),&written,0);std::vector<UCHAR> object(size),buffer(1024*1024);UCHAR digest[32]{};
    bool ok=BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)>=0;
    std::ifstream file(path,std::ios::binary);ok&=file.good();
    while(ok&&file){file.read((char*)buffer.data(),buffer.size());if(file.gcount())ok=BCryptHashData(hash,buffer.data(),(ULONG)file.gcount(),0)>=0;}
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);if(!ok)return {};
    std::string result;for(auto byte:digest){char s[3];sprintf_s(s,"%02x",byte);result+=s;}return result;
}
inline bool SupportedBuild(const std::filesystem::path& root){
    struct Expected {const wchar_t* file;const char* hash;};
    const Expected expected[]={
        {L"Fall of Avalon.exe","e01f1a4cfa8ebb93f7d5c9ea3925cef2883e1d230741ede76ad4f4c7d1bcd594"},
        {L"UnityPlayer.dll","b18b328000c79f5dc1d3eb6762dfbc97cd878ae150eb8559b913dca2ea298d04"},
        {L"GameAssembly.dll","1f4a3e782506cdf8f6cc85655a8e46d40cd84e49dc6ee3a0ed7182ba858b7033"}
    };
    for(auto& e:expected)if(SHA256(root/e.file)!=e.hash){fwprintf(stderr,L"Unsupported or unreadable build: %s\n",e.file);return false;}
    return true;
}
