if get_option('with_xml_pack')

xml_pack_resources = gnome.compile_resources(
  'xml-pack-resources',
  'xml-pack.gresource.xml',
  c_name: 'ide_xml'
)

xml_pack_sources = [
  'ide-xml-analysis.c',
  'ide-xml-analysis.h',
  'ide-xml-completion-attributes.c',
  'ide-xml-completion-attributes.h',
  'ide-xml-completion-values.c',
  'ide-xml-completion-values.h',
  'ide-xml-completion-provider.c',
  'ide-xml-completion-provider.h',
  'ide-xml-diagnostic-provider.c',
  'ide-xml-diagnostic-provider.h',
  'ide-xml-hash-table.c',
  'ide-xml-hash-table.h',
  'ide-xml-highlighter.c',
  'ide-xml-highlighter.h',
  'ide-xml-indenter.c',
  'ide-xml-indenter.h',
  'ide-xml-parser.c',
  'ide-xml-parser.h',
  'ide-xml-parser-generic.c',
  'ide-xml-parser-generic.h',
  'ide-xml-parser-private.h',
  'ide-xml-parser-ui.c',
  'ide-xml-parser-ui.h',
  'ide-xml-path.c',
  'ide-xml-path.h',
  'ide-xml-position.c',
  'ide-xml-position.h',
  'ide-xml-rng-define.c',
  'ide-xml-rng-define.h',
  'ide-xml-rng-grammar.c',
  'ide-xml-rng-grammar.h',
  'ide-xml-rng-parser.c',
  'ide-xml-rng-parser.h',
  'ide-xml-sax.c',
  'ide-xml-sax.h',
  'ide-xml-schema.c',
  'ide-xml-schema.h',
  'ide-xml-schema-cache-entry.c',
  'ide-xml-schema-cache-entry.h',
  'ide-xml-service.c',
  'ide-xml-service.h',
  'ide-xml-stack.c',
  'ide-xml-stack.h',
  'ide-xml-symbol-node.c',
  'ide-xml-symbol-node.h',
  'ide-xml-symbol-resolver.c',
  'ide-xml-symbol-resolver.h',
  'ide-xml-symbol-tree.c',
  'ide-xml-symbol-tree.h',
  'ide-xml-tree-builder.c',
  'ide-xml-tree-builder.h',
  'ide-xml-tree-builder-utils.c',
  'ide-xml-tree-builder-utils-private.h',
  'ide-xml-types.h',
  'ide-xml-utils.c',
  'ide-xml-utils.h',
  'ide-xml-validator.c',
  'ide-xml-validator.h',
  'ide-xml.c',
  'ide-xml.h',
  'xml-pack-plugin.c',
]

gnome_builder_plugins_sources += files(xml_pack_sources)
gnome_builder_plugins_sources += xml_pack_resources[0]

endif
