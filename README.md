# RinXML

RinXML provides a bounded, allocation-free XML event parser and a structural
validator for a small Office-compatible SVG profile. It is not a full XML
processor, namespace engine, or SVG renderer.

## Supported API and XML profile

The public C API is in `include/rinxml/xml.h`: initialize a caller-owned
`RinXmlParser`, retrieve events with `rin_xml_parser_next`, or validate a
complete document with `rin_xml_validate`.

The parser accepts UTF-8 documents with one root element, matching start/end
names, quoted attributes, text, comments, CDATA, processing instructions, and
an optional XML declaration. Attribute duplicates are rejected. It validates
the five predefined entity references and numeric character references, but
returns source slices and does not expand references. Namespace declaration
attributes are identified; namespace scopes and expanded names are not
resolved. Names use the parser's bounded ASCII name grammar.

DTD and other unsupported markup are rejected. External entities are never
loaded or resolved. This subset must not be advertised as full XML 1.0
compatibility.

## SVG validation profile

`include/rinxml/svg.h` exposes `rin_svg_validate`. It validates an SVG root
with required `width` and `height`, optional SVG namespace declaration, and
empty `rect`, `circle`, or `line` children with only their documented
attributes. Inter-element text must be whitespace. Comments and an XML
declaration are accepted; CDATA, processing instructions, nested elements,
other SVG elements, unknown attributes, and entity references in SVG
attributes are rejected.

This is structural admission only. It does not parse geometry values, render
pixels, load resources, resolve URIs, or sanitize content for a separate SVG
engine.

## Ownership, thread safety, and limits

The caller owns the input bytes and parser storage. Event names, text, and
attributes are non-owning slices into the input or the parser's attribute
array; attribute storage is reused on the next parser call. A parser may be
used by one caller at a time. Independent parser instances and inputs can be
used concurrently; the implementation keeps no mutable global parser state.

Default XML limits are 16 MiB total input, depth 128, 1,048,576 elements, 64
attributes per element, 1 MiB text per token, and 255 bytes per name. SVG
defaults are 8 MiB input, depth 64, 2048 elements, 8 attributes per element,
and 4096 bytes markup/text. Callers may lower these limits; maximum depth and
attribute count cannot exceed the fixed parser storage bounds.

`RinXmlStatus` distinguishes invalid arguments, malformed input, limits,
unsupported markup/profile, success, and end-of-document. Parsing is
allocation-free but has no cancellation or CPU deadline; byte, element, and
token limits remain necessary for untrusted input.

The parent repository's sanitizer CI fuzzes both the event parser and SVG
validator with generated valid and malformed seeds. Its per-input limits are
64 KiB, depth 64, 4096 elements, 32 attributes per element, and 8192 bytes per
text token; libFuzzer adds a two-second input timeout and a 512 MiB process RSS
cap. The parser remains allocation-free and the host timeout is not a runtime
deadline API.

## ABI, build, and tests

The public C structs and functions are source-level interfaces without a
separately versioned binary ABI promise. The repository contains no standalone
build or test target; consumers integrate `xml.c` and, when needed, `svg.c`
through their parent build.
