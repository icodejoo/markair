// T20 覆盖测试:ExpandAttribute 的实体解码 / 空属性 / 带空格文本,以及
// ClassifyLinkTarget 的链接种类判定(含 data: 前缀识别)。
#include "markair_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/attr.h"

#include <cstring>

using markair::Arena;
using markair::ClassifyLinkTarget;
using markair::ExpandAttribute;
using markair::LinkTargetKind;
using markair::StrSlice;
using markair::u32;

#define MARKAIR_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

namespace {

// 构造一个单分段(整体同一种 substr_type)的 MD_ATTRIBUTE。
MD_ATTRIBUTE MakeSingleSegAttr(const char* text, u32 len, MD_TEXTTYPE type,
                               MD_TEXTTYPE* typeBuf, MD_OFFSET* offBuf) {
    typeBuf[0] = type;
    offBuf[0] = 0;
    offBuf[1] = len;
    MD_ATTRIBUTE attr{};
    attr.text = text;
    attr.size = static_cast<MD_SIZE>(len);
    attr.substr_types = typeBuf;
    attr.substr_offsets = offBuf;
    return attr;
}

// 把两段拼在一起(如 "normal" + "entity")构造一个双分段 MD_ATTRIBUTE。
MD_ATTRIBUTE MakeTwoSegAttr(const char* text, u32 len1, u32 len2,
                            MD_TEXTTYPE t1, MD_TEXTTYPE t2,
                            MD_TEXTTYPE* typeBuf, MD_OFFSET* offBuf) {
    typeBuf[0] = t1;
    typeBuf[1] = t2;
    offBuf[0] = 0;
    offBuf[1] = len1;
    offBuf[2] = len1 + len2;
    MD_ATTRIBUTE attr{};
    attr.text = text;
    attr.size = static_cast<MD_SIZE>(len1 + len2);
    attr.substr_types = typeBuf;
    attr.substr_offsets = offBuf;
    return attr;
}

} // namespace

// 样本 1:纯普通文本(含空格),原样透传。
MARKAIR_TEST(Attr_NormalTextWithSpaces) {
    MARKAIR_MAKE_TEST_ARENA();
    const char text[] = "images/my photo.png";
    MD_TEXTTYPE types[1];
    MD_OFFSET offs[2];
    MD_ATTRIBUTE attr = MakeSingleSegAttr(text, sizeof(text) - 1, MD_TEXT_NORMAL, types, offs);

    StrSlice out = ExpandAttribute(attr, &arena);
    MARKAIR_CHECK_EQ(out.len, sizeof(text) - 1);
    MARKAIR_CHECK(memcmp(out.data, text, out.len) == 0);
}

// 样本 2:空属性(如无 title 时)返回长度为 0 的切片。
MARKAIR_TEST(Attr_EmptyAttribute) {
    MARKAIR_MAKE_TEST_ARENA();
    MD_ATTRIBUTE attr{};
    attr.text = nullptr;
    attr.size = 0;
    attr.substr_types = nullptr;
    attr.substr_offsets = nullptr;

    StrSlice out = ExpandAttribute(attr, &arena);
    MARKAIR_CHECK_EQ(out.len, 0u);
}

// 样本 3:命名实体解码(&amp; &lt; &gt; &quot; &#39;)。
MARKAIR_TEST(Attr_NamedEntities) {
    MARKAIR_MAKE_TEST_ARENA();
    struct Case { const char* entity; char expected; };
    const Case cases[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#39;", '\''},
    };
    for (const Case& c : cases) {
        u32 len = static_cast<u32>(strlen(c.entity));
        MD_TEXTTYPE types[1];
        MD_OFFSET offs[2];
        MD_ATTRIBUTE attr = MakeSingleSegAttr(c.entity, len, MD_TEXT_ENTITY, types, offs);
        StrSlice out = ExpandAttribute(attr, &arena);
        MARKAIR_CHECK_EQ(out.len, 1u);
        if (out.len == 1) MARKAIR_CHECK_EQ(out.data[0], c.expected);
    }
}

// 样本 4:十六进制数字实体解码为多字节 UTF-8(😀 = U+1F600)。
MARKAIR_TEST(Attr_HexNumericEntity) {
    MARKAIR_MAKE_TEST_ARENA();
    const char entity[] = "&#x1F600;";
    MD_TEXTTYPE types[1];
    MD_OFFSET offs[2];
    MD_ATTRIBUTE attr = MakeSingleSegAttr(entity, sizeof(entity) - 1, MD_TEXT_ENTITY, types, offs);

    StrSlice out = ExpandAttribute(attr, &arena);
    const unsigned char expected[] = {0xF0, 0x9F, 0x98, 0x80}; // U+1F600 的 UTF-8 编码
    MARKAIR_CHECK_EQ(out.len, 4u);
    if (out.len == 4) MARKAIR_CHECK(memcmp(out.data, expected, 4) == 0);
}

// 样本 5:十进制数字实体解码。
MARKAIR_TEST(Attr_DecimalNumericEntity) {
    MARKAIR_MAKE_TEST_ARENA();
    const char entity[] = "&#65;"; // 'A'
    MD_TEXTTYPE types[1];
    MD_OFFSET offs[2];
    MD_ATTRIBUTE attr = MakeSingleSegAttr(entity, sizeof(entity) - 1, MD_TEXT_ENTITY, types, offs);

    StrSlice out = ExpandAttribute(attr, &arena);
    MARKAIR_CHECK_EQ(out.len, 1u);
    if (out.len == 1) MARKAIR_CHECK_EQ(out.data[0], 'A');
}

// 样本 6:未识别命名实体退化为字面文本。
MARKAIR_TEST(Attr_UnknownNamedEntityFallsBackToLiteral) {
    MARKAIR_MAKE_TEST_ARENA();
    const char entity[] = "&foobar;";
    MD_TEXTTYPE types[1];
    MD_OFFSET offs[2];
    MD_ATTRIBUTE attr = MakeSingleSegAttr(entity, sizeof(entity) - 1, MD_TEXT_ENTITY, types, offs);

    StrSlice out = ExpandAttribute(attr, &arena);
    MARKAIR_CHECK_EQ(out.len, sizeof(entity) - 1);
    if (out.len == sizeof(entity) - 1) MARKAIR_CHECK(memcmp(out.data, entity, out.len) == 0);
}

// 样本 7:NULLCHAR 段替换为 U+FFFD。
MARKAIR_TEST(Attr_NullCharReplaced) {
    MARKAIR_MAKE_TEST_ARENA();
    const char raw[] = "x";
    MD_TEXTTYPE types[1];
    MD_OFFSET offs[2];
    MD_ATTRIBUTE attr = MakeSingleSegAttr(raw, 1, MD_TEXT_NULLCHAR, types, offs);

    StrSlice out = ExpandAttribute(attr, &arena);
    const unsigned char expected[] = {0xEF, 0xBF, 0xBD};
    MARKAIR_CHECK_EQ(out.len, 3u);
    if (out.len == 3) MARKAIR_CHECK(memcmp(out.data, expected, 3) == 0);
}

// 样本 8:普通文本 + 实体混合的多分段属性(模拟 title="foo &quot; bar" 的场景)。
MARKAIR_TEST(Attr_MixedSegments) {
    MARKAIR_MAKE_TEST_ARENA();
    const char text[] = "foo &quot;";
    MD_TEXTTYPE types[2];
    MD_OFFSET offs[3];
    MD_ATTRIBUTE attr = MakeTwoSegAttr(text, 4, 6, MD_TEXT_NORMAL, MD_TEXT_ENTITY, types, offs);

    StrSlice out = ExpandAttribute(attr, &arena);
    MARKAIR_CHECK_EQ(out.len, 5u); // "foo " + "\""
    if (out.len == 5) MARKAIR_CHECK(memcmp(out.data, "foo \"", 5) == 0);
}

// 样本 9:ClassifyLinkTarget 各分支。
MARKAIR_TEST(Attr_ClassifyLinkTarget) {
    MARKAIR_CHECK(ClassifyLinkTarget(StrSlice{"", 0}) == LinkTargetKind::Unknown);
    MARKAIR_CHECK(ClassifyLinkTarget(StrSlice{"#section", 8}) == LinkTargetKind::Anchor);
    const char dataUri[] = "data:image/png;base64,AAA=";
    MARKAIR_CHECK(ClassifyLinkTarget(StrSlice{dataUri, sizeof(dataUri) - 1}) == LinkTargetKind::DataUri);
    const char httpUrl[] = "https://example.com/a";
    MARKAIR_CHECK(ClassifyLinkTarget(StrSlice{httpUrl, sizeof(httpUrl) - 1}) == LinkTargetKind::External);
    const char relative[] = "./images/a.png";
    MARKAIR_CHECK(ClassifyLinkTarget(StrSlice{relative, sizeof(relative) - 1}) == LinkTargetKind::RelativePath);
}
