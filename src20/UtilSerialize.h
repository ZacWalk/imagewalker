// ImageWalker by Zac Walker
//
// Purpose: The archive used to serialize an image and its metadata to and
//          from a byte stream.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once


namespace IW
{
	namespace Serialize
	{
		

		class NamedType
		{
		public:

			NamedType() : _type(0) { }
			NamedType(DWORD type) : _type(type) { }
			NamedType(const NamedType &other) : _type(other._type) { }

			DWORD _type;

			bool operator==(const DWORD type) const { return _type == type; }
			bool operator==(const NamedType &other) const { return _type == other._type; }
			DWORD operator|(const DWORD type) const { return _type | type; }			
			bool operator<(const NamedType &other) const { return _type < other._type; }

			DWORD GetProperty() const { return _type & ~TypeMask; };
			DWORD GetType() const { return _type & TypeMask; };

			enum
			{
				TypeMask =  0xFF000000,
				NodeStart = 0x01000000,
				NodeEnd =   0x02000000,
				String =    0x04000000,
				Integer =   0x08000000,				
				Blob =      0x10000000,
				Date =      0x20000000,
				Exit =      0x80000000,
			};

			enum
			{
				Flags = 1,
				TimeDelay,
				BackGround,
				Transparent,
				PageLeft,
				PageTop,
				PageRight,
				PageBottom,
				Format,
				ImageData,
				XPelsPerMeter,
				YPelsPerMeter,
				Statistics,
				LoaderName,
				Errors,
				Warnings,
				OriginalImageX,
				OriginalImageY,
				OriginalBpp,
				Pages,
				MetaData,				
				Type,
				SubType,
				Orientation,
				TimeTaken,				
				IsoSpeed,
				WhiteBalance,
				ApertureN,
				ApertureD,
				ExposureTimeN,
				ExposureTimeD,
				FocalLengthN,
				FocalLengthD,
				DateTaken,
				ExposureTime35mm,
				Title,
				Tags,
				Description,
				ObjectName,
			};
		};
		

		class ArchiveBase
		{
		protected:			

			class Node
			{
			public:
				typedef std::vector<Node*> NODELIST;
				typedef std::map<NamedType, std::string> STRINGMAP;
				typedef std::map<NamedType, int> INTMAP;
				typedef std::map<NamedType, IW::FileTime> DATEMAP;
				typedef std::map<NamedType, SimpleBlob> BLOBMAP;

				NamedType _type;
				NODELIST _children;
				STRINGMAP _strings;
				INTMAP _integers;
				BLOBMAP _blobs;
				DATEMAP _dates;
				Node *_pParent;

				
				Node(Node *pParent) : _pParent(pParent) {};
				Node(Node *pParent, NamedType type) : _type(type), _pParent(pParent) {};
				Node(const Node &other) { Copy(other); };
				void operator=(const Node &other) { Copy(other); };

				~Node()
				{
					Clear();
				}

				

				void Clear()
				{
					std::for_each(_children.begin(), _children.end(), IW::delete_object());

					_children.clear();
					_strings.clear();
					_integers.clear();
					_blobs.clear();
					_dates.clear();
				}

				struct CloneChild
				{
					Node *_pParent;
					CloneChild(Node *pParent) : _pParent(pParent) {};

					template <typename T>
					void operator()(T &it){ _pParent->Add(new Node(*it)); }
				};

				void Copy(const Node &other)
				{
					Clear();

					_pParent = other._pParent;
					_strings = other._strings;
					_integers = other._integers;
					_blobs = other._blobs;
					_dates = other._dates;
					_type = other._type;

					CloneChild cloner(this);
					std::for_each(other._children.begin(), other._children.end(), cloner);
				}

				template<class TChannel>
				void Serialize(TChannel &channel)
				{
					channel(_strings);
					channel(_integers);
					channel(_blobs);
					channel(_dates);
					channel(_children);				
				}				

				void Add(Node *pNode)
				{
					_children.push_back(pNode);
				}

				void Add(const NamedType &type, LPCTSTR szValue)
				{
					Add(type, std::string(CT2A(szValue)));
				}

				void Add(const NamedType &type, std::string &strValue)
				{
					_strings[type] = strValue;
				}

				void Add(const NamedType &type, int nValue)
				{
					_integers[type] = nValue;
				}

				void Add(const NamedType &type, SimpleBlob &blob)
				{
					_blobs[type] = blob;
				}

				void Add(const NamedType &type, const FileTime &date)
				{
					_dates[type] = date;
				}		

				void Get(const NamedType &type, CString &strValue)
				{
					STRINGMAP::iterator it = _strings.find(type);
					if (it != _strings.end()) strValue = it->second.c_str();
				}

				void Get(const NamedType &type, int &nValue)
				{
					INTMAP::iterator it = _integers.find(type);
					if (it != _integers.end()) nValue = it->second;
				}

				void Get(const NamedType &type, DWORD &dwValue)
				{
					INTMAP::iterator it = _integers.find(type);
					if (it != _integers.end()) dwValue = it->second;
				}

				void Get(const NamedType &type, long &nValue)
				{
					INTMAP::iterator it = _integers.find(type);
					if (it != _integers.end()) nValue = it->second;
				}

				void Get(const NamedType &type, IW::PixelFormat &pf)
				{
					INTMAP::iterator it = _integers.find(type);
					if (it != _integers.end()) pf = (IW::PixelFormat::Format)it->second;
				}

				void Get(const NamedType &type, IW::FileTime &date)
				{
					DATEMAP::iterator it = _dates.find(type);
					if (it != _dates.end()) date = it->second;
				}
					

				template<class TBlob>
				void GetBlob(const NamedType &type, TBlob &blob)
				{
					BLOBMAP::iterator it = _blobs.find(type);
					if (it != _blobs.end()) blob.CopyData(it->second.GetData(), it->second.GetDataSize());
				}
				
			};

			Node _root;
			Node *_pCurrentNode;			

		public:		

			ArchiveBase() : _root(0), _pCurrentNode(&_root)
			{				
			}	

			int GetSubNodeCount() const
			{
				return _root._children.size();
			}

			friend class ArchiveSection;
		};

		class ArchiveSection
		{
		protected:
			ArchiveBase *_pArchive;

		public:

			ArchiveSection(ArchiveBase *pArchive, const NamedType &type) : _pArchive(pArchive) 
			{
				ArchiveBase::Node *pNewNode = new ArchiveBase::Node(_pArchive->_pCurrentNode, type);
				_pArchive->_pCurrentNode->Add(pNewNode);
				_pArchive->_pCurrentNode = pNewNode;
			};

			ArchiveSection(ArchiveBase *pArchive, ArchiveBase::Node *pNewNode) : _pArchive(pArchive) 
			{
				_pArchive->_pCurrentNode = pNewNode;
			};

			~ArchiveSection()
			{
				_pArchive->_pCurrentNode = _pArchive->_pCurrentNode->_pParent;
				assert(_pArchive->_pCurrentNode != 0);
			}
		};

		class ArchiveStore : public ArchiveBase
		{
		public:

			template<class TValue>
			void operator()(const NamedType type, const TValue &value)
			{
				_pCurrentNode->Add(type, value);
			}

			template<class TCollection>
			void BlobList(const NamedType type, TCollection &collection)
			{
				for(TCollection::iterator i = collection.begin(); i != collection.end(); ++i)
				{
					ArchiveSection section(this, type);
					i->Serialize(*this);
				}
			}	

			template<class TBlob>
			void Blob(const NamedType type, TBlob &blob)
			{
				if (!blob.IsEmpty())
				{
					_pCurrentNode->Add(type, SimpleBlob(blob.GetData(), blob.GetDataSize()));
				}
			}

			template<class TStream>
			class StoreAdapter
			{
			private:
				TStream &_stream;

			public:
				StoreAdapter(TStream &stream) : _stream(stream) {};

				template<class TCollection>
				void operator()(TCollection &collection)
				{
					std::for_each(collection.begin(), collection.end(), *this);
				}							

				void operator()(Node::STRINGMAP::reference ref)
				{
					WriteString(ref.first, ref.second); 
				}

				void operator()(Node::INTMAP::reference ref)
				{
					WriteInteger(ref.first, ref.second);
				}

				void operator()(Node::DATEMAP::reference ref)
				{
					WriteDate(ref.first, ref.second);
				}

				void operator()(Node::BLOBMAP::reference ref)
				{
					WriteBlob(ref.first, ref.second);
				}

				void operator()(Node* pNode)
				{
					WriteType(pNode->_type | NamedType::NodeStart);
					pNode->Serialize(*this);	
					WriteType(pNode->_type | NamedType::NodeEnd);
				}


				void WriteString(const std::string &str)
				{
					int nLength = str.size();
					_stream.Write(&nLength, sizeof(int));
					if (nLength!=0) _stream.Write(str.c_str(), nLength);
				}

				void WriteType(int type)
				{
					_stream.Write(&type, sizeof(int));
				}

				void WriteString(const NamedType &type, const std::string &str)
				{
					WriteType(type | NamedType::String);
					WriteString(str);
				}

				void WriteInteger(const NamedType &type, const int &n)
				{
					WriteType(type | NamedType::Integer);
					_stream.Write(&n, sizeof(int));
				}

				void WriteDate(const NamedType &type, const FileTime &date)
				{
					WriteType(type | NamedType::Date);
					_stream.Write(&date, sizeof(date));
				}

				void WriteBlob(const NamedType &type, IW::SimpleBlob &blob)
				{
					WriteType(type | NamedType::Blob);

					int nLength = blob.GetDataSize();
					_stream.Write(&nLength, sizeof(int));
					if (nLength != 0) _stream.Write(blob.GetData(), nLength);
				}
			};

			template<class TStream>
			void Store(TStream &stream)
			{
				StoreAdapter<TStream> storer(stream);
				_root.Serialize(storer);	
				storer.WriteType(NamedType::Exit);
			}
				
		};

		class ArchiveLoad : public ArchiveBase
		{
		public:

			template<class TStream>
			ArchiveLoad(TStream &stream)
			{
				LoadAdapter<TStream> loader(stream, &_root);
			}

			template<class TValue>
			void operator()(const NamedType &type, TValue &value)
			{
				_pCurrentNode->Get(type, value);
			}

			template<class TCollection>
			void BlobList(const NamedType &type, TCollection &collection)
			{
				collection.clear();

				for(Node::NODELIST::iterator i = _pCurrentNode->_children.begin(); i != _pCurrentNode->_children.end(); ++i)
				{
					const NamedType typeChild = (*i)->_type;

					if (typeChild == type)
					{
						TCollection::value_type item;
						ArchiveSection section(this, *i);
						item.Serialize(*this);						
						collection.push_back(item);
					}
				}
			}	

			template<class TBlob>
			void Blob(const NamedType &type, TBlob &blob)
			{
				_pCurrentNode->GetBlob(type, blob);
			}

			template<class TStream>
			class LoadAdapter
			{
			private:
				TStream &_stream;
				ArchiveBase::Node *_pNode;

			public:
				LoadAdapter(TStream &stream, ArchiveBase::Node *pNode) : _stream(stream), _pNode(pNode)
				{
					assert(_pNode != 0);
					LoadSection();
				};				

				void LoadSection()
				{
					while(true)
					{
						const NamedType type = ReadType();
						const DWORD nProperty = type.GetProperty();
						const DWORD nType = type.GetType();
						ArchiveBase::Node *pNewNode = 0;

						switch(nType)
						{
						case NamedType::NodeStart:
							pNewNode = new ArchiveBase::Node(_pNode, nProperty);
							_pNode->Add(pNewNode);
							_pNode = pNewNode;
							break;
						case NamedType::NodeEnd:
							_pNode = _pNode->_pParent;
							break;
						case NamedType::String:
							ReadString(nProperty);
							break;
						case NamedType::Integer:
							ReadInteger(nProperty);
							break;
						case NamedType::Date:
							ReadDate(nProperty);
							break;
						case NamedType::Blob:
							ReadBlob(nProperty);
							break;
						case NamedType::Exit:
							assert(_pNode != 0);
							return;
						}
					}
				}

				DWORD ReadType()
				{
					DWORD type;
					Read(&type, sizeof(DWORD));
					return type;
				}

				int ReadLength()
				{
					int nLength = 0;
					Read(&nLength, sizeof(int));
					return nLength;
				}
				

				void ReadString(const NamedType &type)
				{
					int nLength = ReadLength();
					if (nLength < 0) throw IW::invalid_file();
					IW::CBuffer<char> sz(static_cast<size_t>(nLength) + 1);
					sz[nLength] = 0;
					if (nLength!=0) Read(sz, nLength);
					_pNode->Add(type, std::string(sz));
				}

				void ReadInteger(const NamedType &type)
				{
					int n = 0;
					Read(&n, sizeof(int));
					_pNode->Add(type, n);
				}	

				void ReadDate(const NamedType &type)
				{
					FileTime date;
					Read(&date, sizeof(date));
					_pNode->Add(type, date);
				}	

				void ReadBlob(const NamedType &type)
				{
					int nLength = ReadLength();
					if (nLength < 0) throw IW::invalid_file();

					if (nLength != 0)
					{
						SimpleBlob blob;
						blob.Alloc(nLength);
						Read(blob.GetData(), nLength);
						_pNode->Add(type, blob);
					}
				}

				void Read(LPVOID lpBuf, DWORD nCount)
				{
					DWORD dwRead = 0;
					_stream.Read(lpBuf, nCount, &dwRead);
					if (nCount != dwRead) throw IW::invalid_file();
				}
			};

		protected:
		};
	};
};
