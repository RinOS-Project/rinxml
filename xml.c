/* SPDX-License-Identifier: MIT */
#include "include/rinxml/xml.h"

static int xml_space(uint8_t value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int xml_name_first(uint8_t value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') || value == '_' || value == ':';
}

static int xml_name_next(uint8_t value)
{
    return xml_name_first(value) || (value >= '0' && value <= '9') ||
           value == '-' || value == '.';
}

static int xml_slice_equal(RinXmlSlice left, RinXmlSlice right)
{
    size_t index;
    if (left.size != right.size) return 0;
    for (index = 0u; index < left.size; ++index)
        if (left.data[index] != right.data[index]) return 0;
    return 1;
}

static int xml_slice_literal(RinXmlSlice value, const char* literal)
{
    size_t size = 0u;
    while (literal[size] != '\0') ++size;
    if (value.size != size) return 0;
    for (size = 0u; size < value.size; ++size)
        if (value.data[size] != (uint8_t)literal[size]) return 0;
    return 1;
}

static int xml_utf8_decode(const uint8_t* data, size_t size, size_t* consumed)
{
    uint8_t first;
    size_t length;
    uint32_t value;
    size_t index;
    if (data == NULL || size == 0u || consumed == NULL) return 0;
    first = data[0];
    if (first < 0x80u) {
        *consumed = 1u;
        return first != 0u && first != 0x7fu &&
               (first >= 0x20u || first == '\t' || first == '\r' ||
                first == '\n');
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        length = 2u;
        value = first & 0x1fu;
    } else if (first >= 0xe0u && first <= 0xefu) {
        length = 3u;
        value = first & 0x0fu;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        length = 4u;
        value = first & 0x07u;
    } else {
        return 0;
    }
    if (length > size) return 0;
    for (index = 1u; index < length; ++index) {
        uint8_t next = data[index];
        if ((next & 0xc0u) != 0x80u) return 0;
        value = (value << 6u) | (next & 0x3fu);
    }
    if ((length == 2u && value < 0x80u) ||
        (length == 3u && value < 0x800u) ||
        (length == 4u && value < 0x10000u) || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) return 0;
    *consumed = length;
    return 1;
}

static int xml_utf8_valid(const uint8_t* data, size_t size)
{
    size_t offset = 0u;
    while (offset < size) {
        size_t consumed = 0u;
        if (!xml_utf8_decode(data + offset, size - offset, &consumed))
            return 0;
        offset += consumed;
    }
    return 1;
}

static int xml_numeric_reference(const uint8_t* data, size_t size,
                                 size_t* consumed)
{
    size_t index = 2u;
    uint32_t value = 0u;
    int hexadecimal = 0;
    if (size < 4u || data[0] != '&' || data[1] != '#') return 0;
    if (index < size && (data[index] == 'x' || data[index] == 'X')) {
        hexadecimal = 1;
        ++index;
    }
    if (index >= size) return 0;
    if (index >= size || data[index] == ';') return 0;
    for (; index < size && data[index] != ';'; ++index) {
        uint32_t digit;
        if (hexadecimal) {
            if (data[index] >= '0' && data[index] <= '9')
                digit = data[index] - '0';
            else if (data[index] >= 'a' && data[index] <= 'f')
                digit = data[index] - 'a' + 10u;
            else if (data[index] >= 'A' && data[index] <= 'F')
                digit = data[index] - 'A' + 10u;
            else return 0;
            if (value > (0x10ffffu - digit) / 16u) return 0;
            value = value * 16u + digit;
        } else {
            if (data[index] < '0' || data[index] > '9') return 0;
            digit = data[index] - '0';
            if (value > (0x10ffffu - digit) / 10u) return 0;
            value = value * 10u + digit;
        }
    }
    if (index == size || value == 0u || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) return 0;
    *consumed = index + 1u;
    return 1;
}

static int xml_reference(const uint8_t* data, size_t size, size_t* consumed)
{
    static const char* const names[] = {"&amp;", "&lt;", "&gt;", "&apos;",
                                        "&quot;"};
    size_t index;
    if (data == NULL || size == 0u || consumed == NULL || data[0] != '&')
        return 0;
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); ++index) {
        size_t length = 0u;
        while (names[index][length] != '\0') ++length;
        if (size >= length) {
            size_t candidate;
            for (candidate = 0u; candidate < length; ++candidate)
                if (data[candidate] != (uint8_t)names[index][candidate]) break;
            if (candidate == length) {
                *consumed = length;
                return 1;
            }
        }
    }
    return xml_numeric_reference(data, size, consumed);
}

static int xml_value_valid(const uint8_t* data, size_t size)
{
    size_t offset = 0u;
    while (offset < size) {
        size_t consumed = 0u;
        if (data[offset] == '&') {
            if (!xml_reference(data + offset, size - offset, &consumed))
                return 0;
        } else {
            if (data[offset] == '<' ||
                !xml_utf8_decode(data + offset, size - offset, &consumed))
                return 0;
        }
        offset += consumed;
    }
    return 1;
}

static int xml_parse_name(const uint8_t* data, size_t size, size_t* offset,
                          size_t max_name, RinXmlSlice* name)
{
    size_t start;
    if (data == NULL || offset == NULL || name == NULL || *offset >= size ||
        !xml_name_first(data[*offset])) return 0;
    start = *offset;
    ++*offset;
    while (*offset < size && xml_name_next(data[*offset])) ++*offset;
    if (*offset - start > max_name) return 0;
    name->data = data + start;
    name->size = *offset - start;
    return 1;
}

static void xml_skip_space(const uint8_t* data, size_t size, size_t* offset)
{
    while (*offset < size && xml_space(data[*offset])) ++*offset;
}

static const uint8_t* xml_find(const uint8_t* data, size_t size,
                               const char* literal, size_t literal_size)
{
    size_t offset;
    if (data == NULL || literal == NULL || literal_size == 0u ||
        literal_size > size) return NULL;
    for (offset = 0u; offset + literal_size <= size; ++offset) {
        size_t index;
        for (index = 0u; index < literal_size; ++index)
            if (data[offset + index] != (uint8_t)literal[index]) break;
        if (index == literal_size) return data + offset;
    }
    return NULL;
}

static int xml_name_is_namespace(RinXmlSlice name)
{
    return xml_slice_literal(name, "xmlns") ||
           (name.size > 6u && name.data[0] == 'x' && name.data[1] == 'm' &&
            name.data[2] == 'l' && name.data[3] == 'n' && name.data[4] == 's' &&
            name.data[5] == ':');
}

void rin_xml_limits_default(RinXmlLimits* limits)
{
    if (limits == NULL) return;
    limits->max_depth = RIN_XML_DEFAULT_MAX_DEPTH;
    limits->max_elements = RIN_XML_DEFAULT_MAX_ELEMENTS;
    limits->max_attributes_per_element = RIN_XML_DEFAULT_MAX_ATTRIBUTES;
    limits->max_total_bytes = RIN_XML_DEFAULT_MAX_TOTAL_BYTES;
    limits->max_text_bytes = RIN_XML_DEFAULT_MAX_TEXT_BYTES;
    limits->max_name_bytes = RIN_XML_DEFAULT_MAX_NAME_BYTES;
}

int rin_xml_parser_init(RinXmlParser* parser, const uint8_t* data, size_t size,
                        const RinXmlLimits* limits)
{
    RinXmlLimits defaults;
    if (parser != NULL) *parser = (RinXmlParser){0};
    if (parser == NULL || data == NULL || size == 0u) return RIN_XML_INVALID_ARGUMENT;
    rin_xml_limits_default(&defaults);
    if (limits != NULL) defaults = *limits;
    if (defaults.max_depth == 0u || defaults.max_depth > RIN_XML_MAX_DEPTH ||
        defaults.max_elements == 0u || defaults.max_attributes_per_element == 0u ||
        defaults.max_attributes_per_element > RIN_XML_MAX_ATTRIBUTES ||
        defaults.max_total_bytes == 0u || defaults.max_text_bytes == 0u ||
        defaults.max_name_bytes == 0u || size > defaults.max_total_bytes ||
        !xml_utf8_valid(data, size)) return RIN_XML_LIMIT;
    parser->data = data;
    parser->size = size;
    parser->offset = 0u;
    parser->depth = 0u;
    parser->element_count = 0u;
    parser->root_count = 0u;
    parser->max_depth = defaults.max_depth;
    parser->max_elements = defaults.max_elements;
    parser->max_attributes_per_element = defaults.max_attributes_per_element;
    parser->max_text_bytes = defaults.max_text_bytes;
    parser->max_name_bytes = defaults.max_name_bytes;
    parser->saw_root = 0u;
    parser->saw_declaration = 0u;
    parser->finished = 0u;
    return RIN_XML_OK;
}

static int xml_parse_start(RinXmlParser* parser, RinXmlEvent* event)
{
    size_t offset = parser->offset + 1u;
    size_t attribute_count = 0u;
    size_t name_end;
    RinXmlSlice name;
    uint8_t self_closing = 0u;
    if (!xml_parse_name(parser->data, parser->size, &offset,
                        parser->max_name_bytes, &name) ||
        parser->element_count >= parser->max_elements) return RIN_XML_LIMIT;
    ++parser->element_count;
    name_end = offset;
    for (;;) {
        RinXmlSlice attribute_name;
        RinXmlSlice attribute_value;
        size_t value_start;
        size_t index;
        uint8_t quote;
        xml_skip_space(parser->data, parser->size, &offset);
        if (offset >= parser->size) return RIN_XML_MALFORMED;
        if (parser->data[offset] == '>') {
            ++offset;
            break;
        }
        if (parser->data[offset] == '/' && offset + 1u < parser->size &&
            parser->data[offset + 1u] == '>') {
            offset += 2u;
            self_closing = 1u;
            break;
        }
        if (attribute_count >= parser->max_attributes_per_element ||
            !xml_parse_name(parser->data, parser->size, &offset,
                            parser->max_name_bytes, &attribute_name))
            return RIN_XML_LIMIT;
        for (index = 0u; index < attribute_count; ++index)
            if (xml_slice_equal(parser->attributes[index].name, attribute_name))
                return RIN_XML_MALFORMED;
        xml_skip_space(parser->data, parser->size, &offset);
        if (offset >= parser->size || parser->data[offset++] != '=')
            return RIN_XML_MALFORMED;
        xml_skip_space(parser->data, parser->size, &offset);
        if (offset >= parser->size || (parser->data[offset] != '\'' &&
                                       parser->data[offset] != '"'))
            return RIN_XML_MALFORMED;
        quote = parser->data[offset++];
        value_start = offset;
        while (offset < parser->size && parser->data[offset] != quote) ++offset;
        if (offset >= parser->size || offset - value_start > parser->max_text_bytes ||
            !xml_value_valid(parser->data + value_start, offset - value_start))
            return RIN_XML_LIMIT;
        attribute_value.data = parser->data + value_start;
        attribute_value.size = offset - value_start;
        ++offset;
        parser->attributes[attribute_count].name = attribute_name;
        parser->attributes[attribute_count].value = attribute_value;
        parser->attributes[attribute_count].is_namespace_declaration =
            (uint8_t)xml_name_is_namespace(attribute_name);
        ++attribute_count;
    }
    (void)name_end;
    if (self_closing != 0u && parser->depth >= parser->max_depth)
        return RIN_XML_LIMIT;
    if (self_closing == 0u) {
        if (parser->depth >= parser->max_depth) return RIN_XML_LIMIT;
        parser->stack[parser->depth++] = name;
    }
    if (self_closing != 0u && parser->depth == 0u) {
        if (parser->saw_root != 0u || ++parser->root_count != 1u)
            return RIN_XML_MALFORMED;
        parser->saw_root = 1u;
    } else if (self_closing == 0u && parser->depth == 1u) {
        if (parser->saw_root != 0u || ++parser->root_count != 1u)
            return RIN_XML_MALFORMED;
        parser->saw_root = 1u;
    } else if (parser->saw_root == 0u) {
        return RIN_XML_MALFORMED;
    }
    event->type = RIN_XML_EVENT_START_ELEMENT;
    event->name = name;
    event->text.data = NULL;
    event->text.size = 0u;
    event->attributes = parser->attributes;
    event->attribute_count = attribute_count;
    event->depth = self_closing != 0u ? parser->depth + 1u : parser->depth;
    event->self_closing = self_closing;
    parser->offset = offset;
    return RIN_XML_OK;
}

static int xml_parse_close(RinXmlParser* parser, RinXmlEvent* event)
{
    size_t offset = parser->offset + 2u;
    RinXmlSlice name;
    if (parser->depth == 0u ||
        !xml_parse_name(parser->data, parser->size, &offset,
                        parser->max_name_bytes, &name))
        return RIN_XML_MALFORMED;
    xml_skip_space(parser->data, parser->size, &offset);
    if (offset >= parser->size || parser->data[offset] != '>' ||
        !xml_slice_equal(name, parser->stack[parser->depth - 1u]))
        return RIN_XML_MALFORMED;
    ++offset;
    event->type = RIN_XML_EVENT_END_ELEMENT;
    event->name = name;
    event->text.data = NULL;
    event->text.size = 0u;
    event->attributes = NULL;
    event->attribute_count = 0u;
    event->depth = parser->depth;
    event->self_closing = 0u;
    --parser->depth;
    parser->offset = offset;
    return RIN_XML_OK;
}

static int xml_parse_markup(RinXmlParser* parser, RinXmlEvent* event)
{
    size_t offset = parser->offset;
    const uint8_t* end;
    if (parser->size - offset >= 4u &&
        parser->data[offset + 1u] == '!' &&
        parser->data[offset + 2u] == '-' && parser->data[offset + 3u] == '-') {
        size_t body_start = offset + 4u;
        end = xml_find(parser->data + body_start, parser->size - body_start,
                       "-->", 3u);
        if (end == NULL || (size_t)(end - (parser->data + body_start)) >
                              parser->max_text_bytes)
            return RIN_XML_LIMIT;
        if (xml_find(parser->data + body_start,
                     (size_t)(end - (parser->data + body_start)), "--", 2u) != NULL)
            return RIN_XML_MALFORMED;
        event->type = RIN_XML_EVENT_COMMENT;
        event->name.data = NULL;
        event->name.size = 0u;
        event->text.data = parser->data + body_start;
        event->text.size = (size_t)(end - (parser->data + body_start));
        event->attributes = NULL;
        event->attribute_count = 0u;
        event->depth = parser->depth;
        event->self_closing = 0u;
        parser->offset = (size_t)(end - parser->data) + 3u;
        return RIN_XML_OK;
    }
    if (parser->size - offset >= 9u &&
        parser->data[offset + 1u] == '!' && parser->data[offset + 2u] == '[') {
        static const char cdata[] = "<![CDATA[";
        size_t body_start = offset + 9u;
        size_t body_size;
        end = xml_find(parser->data + body_start, parser->size - body_start,
                       "]]>", 3u);
        if (end == NULL || parser->depth == 0u) return RIN_XML_MALFORMED;
        body_size = (size_t)(end - (parser->data + body_start));
        if (body_size > parser->max_text_bytes ||
            !xml_slice_literal((RinXmlSlice){parser->data + offset, 9u}, cdata) ||
            !xml_utf8_valid(parser->data + body_start, body_size))
            return RIN_XML_LIMIT;
        event->type = RIN_XML_EVENT_CDATA;
        event->name.data = NULL;
        event->name.size = 0u;
        event->text.data = parser->data + body_start;
        event->text.size = body_size;
        event->attributes = NULL;
        event->attribute_count = 0u;
        event->depth = parser->depth;
        event->self_closing = 0u;
        parser->offset = (size_t)(end - parser->data) + 3u;
        return RIN_XML_OK;
    }
    if (parser->size - offset >= 2u && parser->data[offset + 1u] == '?') {
        size_t target_offset = offset + 2u;
        size_t body_start;
        RinXmlSlice target;
        if (!xml_parse_name(parser->data, parser->size, &target_offset,
                            parser->max_name_bytes, &target))
            return RIN_XML_MALFORMED;
        end = xml_find(parser->data + target_offset,
                       parser->size - target_offset, "?>", 2u);
        if (end == NULL) return RIN_XML_MALFORMED;
        if (xml_slice_literal(target, "xml")) {
            if (parser->saw_declaration != 0u || parser->saw_root != 0u)
                return RIN_XML_MALFORMED;
            parser->saw_declaration = 1u;
            event->type = RIN_XML_EVENT_DECLARATION;
        } else {
            event->type = RIN_XML_EVENT_PROCESSING_INSTRUCTION;
        }
        body_start = target_offset;
        event->name = target;
        event->text.data = parser->data + body_start;
        event->text.size = (size_t)(end - (parser->data + body_start));
        if (event->text.size > parser->max_text_bytes ||
            !xml_utf8_valid(event->text.data, event->text.size))
            return RIN_XML_LIMIT;
        event->attributes = NULL;
        event->attribute_count = 0u;
        event->depth = parser->depth;
        event->self_closing = 0u;
        parser->offset = (size_t)(end - parser->data) + 2u;
        return RIN_XML_OK;
    }
    if (parser->size - offset >= 9u && parser->data[offset + 1u] == '!' &&
        parser->data[offset + 2u] == 'D' && parser->data[offset + 3u] == 'O' &&
        parser->data[offset + 4u] == 'C' && parser->data[offset + 5u] == 'T' &&
        parser->data[offset + 6u] == 'Y' && parser->data[offset + 7u] == 'P' &&
        parser->data[offset + 8u] == 'E') return RIN_XML_UNSUPPORTED;
    return RIN_XML_UNSUPPORTED;
}

int rin_xml_parser_next(RinXmlParser* parser, RinXmlEvent* event)
{
    if (event != NULL) *event = (RinXmlEvent){0};
    if (parser == NULL || event == NULL || parser->data == NULL)
        return RIN_XML_INVALID_ARGUMENT;
    if (parser->finished != 0u) return RIN_XML_DONE;
    while (parser->offset < parser->size) {
        size_t start = parser->offset;
        if (parser->data[start] != '<') {
            while (parser->offset < parser->size && parser->data[parser->offset] != '<')
                ++parser->offset;
            if (parser->offset - start > parser->max_text_bytes ||
                !xml_value_valid(parser->data + start, parser->offset - start))
                return RIN_XML_LIMIT;
            if (parser->depth == 0u) {
                size_t index;
                for (index = start; index < parser->offset; ++index)
                    if (!xml_space(parser->data[index])) return RIN_XML_MALFORMED;
                continue;
            }
            event->type = RIN_XML_EVENT_TEXT;
            event->name.data = NULL;
            event->name.size = 0u;
            event->text.data = parser->data + start;
            event->text.size = parser->offset - start;
            event->attributes = NULL;
            event->attribute_count = 0u;
            event->depth = parser->depth;
            event->self_closing = 0u;
            return RIN_XML_OK;
        }
        if (parser->offset + 1u >= parser->size) return RIN_XML_MALFORMED;
        if (parser->data[parser->offset + 1u] == '/')
            return xml_parse_close(parser, event);
        if (parser->data[parser->offset + 1u] == '!' ||
            parser->data[parser->offset + 1u] == '?')
            return xml_parse_markup(parser, event);
        return xml_parse_start(parser, event);
    }
    if (parser->depth != 0u || parser->saw_root == 0u || parser->root_count != 1u)
        return RIN_XML_MALFORMED;
    parser->finished = 1u;
    return RIN_XML_DONE;
}

int rin_xml_validate(const uint8_t* data, size_t size,
                     const RinXmlLimits* limits)
{
    RinXmlParser parser;
    RinXmlEvent event;
    int status = rin_xml_parser_init(&parser, data, size, limits);
    if (status != RIN_XML_OK) return status;
    for (;;) {
        status = rin_xml_parser_next(&parser, &event);
        if (status == RIN_XML_DONE) return RIN_XML_OK;
        if (status != RIN_XML_OK) return status;
    }
}
