#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sdk/include/utils.h"

namespace nyx {
namespace sdk {
namespace engine {

// 游戏引擎类型
enum class Kind {
    Unity,
    Unreal,
};

// 引擎探测结果
struct Probe {
    // 探测到的引擎类型
    Kind kind = Kind::Unity;
    // 探测状态
    Status status = Status::Unavailable;
    // 探测详情
    std::string detail;

    // 判断引擎是否可用
    bool available() const {
        return status == Status::Ok;
    }
};

// Unity 镜像
struct Image {
    // il2cpp image 句柄
    const void* handle = nullptr;
    // 镜像名称
    std::string name;
};

// Unity 类
struct Class {
    // il2cpp class 句柄
    void* handle = nullptr;
    // 命名空间
    std::string name_space;
    // 类名
    std::string name;
};

// Unity 方法
struct Method {
    // il2cpp method 句柄
    const void* handle = nullptr;
    // 方法名
    std::string name;
    // 参数数量，-1 表示未指定
    int arg_count = -1;
    // 返回类型
    std::string return_type;
    // 参数类型列表
    std::vector<std::string> params;
};

// Unity 字段
struct Field {
    // il2cpp field 句柄
    void* handle = nullptr;
    // 字段名
    std::string name;
    // 字段类型名
    std::string type_name;
};

// 类查询条件
struct ClassQuery {
    // 所属镜像句柄
    const void* image = nullptr;
    // 命名空间，空值表示全局命名空间
    const char* name_space = nullptr;
    // 类名
    const char* name = nullptr;
};

// 方法查询条件
struct MethodQuery {
    // 所属类句柄
    void* klass = nullptr;
    // 方法名
    const char* name = nullptr;
    // 参数数量，-1 表示不限制
    int arg_count = -1;
    // 参数类型列表
    const char* const* params = nullptr;
    // 参数类型数量
    std::size_t param_count = 0;
};

// 字段查询条件
struct FieldQuery {
    // 所属类句柄
    void* klass = nullptr;
    // 字段名
    const char* name = nullptr;
};

// 方法签名：用于绑定并校验方法
struct MethodSignature {
    // 镜像名
    const char* image_name = nullptr;
    // 类命名空间
    const char* class_namespace = nullptr;
    // 类名
    const char* class_name = nullptr;
    // 方法名
    const char* name = nullptr;
    // 返回类型
    const char* return_type = nullptr;
    // 参数类型列表
    const char* const* params = nullptr;
    // 参数数量
    std::size_t param_count = 0;
};

// 字段签名：用于绑定并校验字段
struct FieldSignature {
    // 镜像名
    const char* image_name = nullptr;
    // 类命名空间
    const char* class_namespace = nullptr;
    // 类名
    const char* class_name = nullptr;
    // 字段名
    const char* name = nullptr;
    // 字段类型名
    const char* type_name = nullptr;
};

// 绑定事件
struct BindingEvent {
    // 事件类型
    std::string kind;
    // 镜像名
    std::string image_name;
    // 类命名空间
    std::string class_namespace;
    // 类名
    std::string class_name;
    // 成员名
    std::string member_name;
    // 绑定状态
    Status status = Status::Ok;
    // 绑定详情
    std::string detail;
};

// 探测 Unity 运行时
NYX_EXPORT Probe IsUnity();
// 探测 Unreal 运行时
NYX_EXPORT Probe IsUnreal();
// 获取 Unity 镜像列表
NYX_EXPORT Result GetImages(std::vector<Image>* out);
// 按名称查找 Unity 镜像
NYX_EXPORT Result FindImage(const char* name, Image* out);
// 查找 Unity 类
NYX_EXPORT Result FindClass(const ClassQuery& query, Class* out);
// 查找 Unity 方法
NYX_EXPORT Result FindMethod(const MethodQuery& query, Method* out);
// 按 image::class::method 路径查找方法
NYX_EXPORT Value<Method> TryFindMethod(const char* path);
// 按镜像、类名和方法名查找方法
NYX_EXPORT Value<Method> TryFindMethod(
    const char* image,
    const char* klass,
    const char* method,
    int arg_count = -1
);
// 按镜像、命名空间、类名和方法名查找方法
NYX_EXPORT Value<Method> TryFindMethod(
    const char* image,
    const char* name_space,
    const char* klass,
    const char* method,
    int arg_count = -1
);
// 按路径查找方法指针，失败时返回 nullptr
NYX_EXPORT void* FindMethod(const char* path);
// 按镜像、类名和方法名查找方法指针
NYX_EXPORT void* FindMethod(const char* image, const char* klass, const char* method, int arg_count = -1);
// 按镜像、命名空间、类名和方法名查找方法指针
NYX_EXPORT void* FindMethod(
    const char* image,
    const char* name_space,
    const char* klass,
    const char* method,
    int arg_count = -1
);
// 从方法记录中取方法指针
NYX_EXPORT void* MethodPtr(const Method& method);
// 查找 Unity 字段
NYX_EXPORT Result FindField(const FieldQuery& query, Field* out);
// 绑定并校验 Unity 方法
NYX_EXPORT Result BindMethod(const MethodSignature& signature, Method* out);
// 绑定并校验 Unity 字段
NYX_EXPORT Result BindField(const FieldSignature& signature, Field* out);
// 清空 Unity 解析缓存
NYX_EXPORT void ClearCache();
// 获取绑定事件
NYX_EXPORT Result GetEvents(std::vector<BindingEvent>* out);
// 清空绑定事件
NYX_EXPORT void ClearEvents();
// 获取被丢弃的绑定事件数量
NYX_EXPORT std::size_t DroppedEvents();

} // namespace engine
} // namespace sdk
} // namespace nyx
