#include "stdafx.h"
#include "iw/imaging/Metadata.h"
#include "ViewMetadataList.h"

void IW::MetadataProperties::Apply(CMetadataListCtrl& list) const
{
	list.SetRedraw(FALSE);

	for (const auto& section : sections)
	{
		if (section.properties.empty())
			continue;

		list.AddCategory(section.name);

		for (const auto& property : section.properties)
			list.AddProperty(property.title, property.value);
	}

	list.SetRedraw(TRUE);
}
