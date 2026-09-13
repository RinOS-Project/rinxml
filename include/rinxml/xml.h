/* SPDX-License-Identifier: MIT */
#ifndef RINXML_XML_H
#define RINXML_XML_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RIN_XML_DEFAULT_MAX_DEPTH 128u
#define RIN_XML_DEFAULT_MAX_ELEMENTS 1048576u
#define RIN_XML_DEFAULT_MAX_ATTRIBUTES 64u
#define RIN_XML_DEFAULT_MAX_TOTAL_BYTES (16u * 1024u * 1024u)
#define RIN_XML_DEFAULT_MAX_TEXT_BYTES (1u * 1024u * 1024u)
#define RIN_XML_DEFAULT_MAX_NAME_BYTES 255u
#define RIN_XML_MAX_DEPTH 128u
#define RIN_XML_MAX_ATTRIBUTES 64u

typedef enum RinXmlStatus {
    RIN_XML_OK = 0,
    RIN_XML_DONE = 1,
    RIN_XML_INVALID_ARGUMENT = -1,
    RIN_XML_MALFORMED = -2,
    RIN_XML_LIMIT = -3,
    RIN_XML_UNSUPPORTED = -4
} RinXmlStatus;

typedef struct RinXmlSlice {
    const uint8_t* data;
    size_t size;
} RinXmlSlice;

typedef struct RinXmlAttribute {
    RinXmlSlice name;
    RinXmlSlice value;
    uint8_t is_namespace_declaration;
} RinXmlAttribute;

typedef enum RinXmlEventType {
    RIN_XML_EVENT_START_ELEMENT = 1,
    RIN_XML_EVENT_END_ELEMENT = 2,
    RIN_XML_EVENT_TEXT = 3,
    RIN_XML_EVENT_CDATA = 4,
    RIN_XML_EVENT_COMMENT = 5,
    RIN_XML_EVENT_DECLARATION = 6,
    RIN_XML_EVENT_PROCESSING_INSTRUCTION = 7
} RinXmlEventType;

typedef struct RinXmlEvent {
    RinXmlEventType type;
    RinXmlSlice name;
    RinXmlSlice text;
    const RinXmlAttribute* attributes;
    size_t attribute_count;
    size_t depth;
    uint8_t self_closing;
} RinXmlEvent;

typedef struct RinXmlLimits {
    size_t max_depth;
    size_t max_elements;
    size_t max_attributes_per_element;
    size_t max_total_bytes;
    size_t max_text_bytes;
    size_t max_name_bytes;
} RinXmlLimits;

typedef struct RinXmlParser {
    const uint8_t* data;
    size_t size;
    size_t offset;
    size_t depth;
    size_t element_count;
    size_t root_count;
    size_t max_depth;
    size_t max_elements;
    size_t max_attributes_per_element;
    size_t max_text_bytes;
    size_t max_name_bytes;
    uint8_t saw_root;
    uint8_t saw_declaration;
    uint8_t finished;
    RinXmlSlice stack[RIN_XML_MAX_DEPTH];
    RinXmlAttribute attributes[RIN_XML_MAX_ATTRIBUTES];
} RinXmlParser;

void rin_xml_limits_default(RinXmlLimits* limits);

int rin_xml_parser_init(RinXmlParser* parser, const uint8_t* data, size_t size,
                        const RinXmlLimits* limits);

/* Returns RIN_XML_OK and fills event, RIN_XML_DONE at the document end, or a
 * negative status. Event slices point into the caller-owned input and remain
 * valid until the next parser call. */
int rin_xml_parser_next(RinXmlParser* parser, RinXmlEvent* event);

/* Validate a complete document without exposing event storage. */
int rin_xml_validate(const uint8_t* data, size_t size,
                     const RinXmlLimits* limits);

#ifdef __cplusplus
}
#endif

#endif /* RINXML_XML_H */
