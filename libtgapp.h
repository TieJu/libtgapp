namespace tga {
#pragma pack(push)
#pragma pack(1)
    struct tga_header {
        unsigned char      _id_length;
        unsigned char      _color_map_type;
        unsigned char      _image_type;
        unsigned short     _color_map_first_entry_index;
        unsigned short     _color_map_length;
        unsigned char      _color_map_entry_size;
        unsigned short     _image_x_origin;
        unsigned short     _image_y_origin;
        unsigned short     _image_width;
        unsigned short     _image_height;
        unsigned char      _image_bits_per_pixel;
        unsigned char      _image_descriptor;
    };
#pragma pack(pop)

#define HAS_MEM_FUNC(func, name)\
    template<typename T, typename Sign>\
    struct name {\
    typedef char yes[1];\
    typedef char no [2];\
    template <typename U, U> struct type_check;\
    template <typename _1> static yes &chk(type_check<Sign, &_1::func > *);\
    template <typename _1> static no  &chk(...);\
    static bool const value = sizeof(chk<T>(0)) == sizeof(yes);\
    }

    namespace detail {
        HAS_MEM_FUNC( data, has_data );
#undef HAS_MEM_FUNC

        template<typename Iterator>
        struct iterator_range {
            Iterator _begin, _end;

            Iterator begin() { return _begin; }
            Iterator end() { return _end; }

            auto front()->typename std::add_reference<decltype( *Iterator {} )>::type { return *_begin; }
            auto back()->typename std::add_reference<decltype( *Iterator {} )>::type { return *( _end - 1 ); }

            void pop_front() { _begin++; }
            void pop_back() { _end--; }

            bool empty() const { return _begin == _end; }
            ptrdiff_t size() const { return std::distance( _begin, _end ); }

            void advance_begin( ptrdiff_t n_ ) {
                std::advance( _begin, n_ );
            }
            void advance_end( ptrdiff_t n_ ) {
                std::advance( _begin, n_ );
            }
        };

        template<typename Iterator>
        inline iterator_range<Iterator> make_iterator_range( Iterator from_, Iterator to_ ) {
            return iterator_range<Iterator>{from_, to_};
        }

        template<typename DataProvider>
        class reader_base {
        protected:
            DataProvider    _src;

            inline static bool is_rle( unsigned char byte_ ) { return ( ( (byte_)& 0x80 ) != 0 ); }
            inline static unsigned get_rle_length( unsigned char byte_ ) { return ( 1 + ( (byte_)& 0x7F ) ); }

            template<typename PixelReciver>
            void read_maped_pixels_rle( PixelReciver clb_ ) {}

            template<typename PixelReciver>
            void read_maped_pixels_raw( PixelReciver clb_ ) {}

        public:
            reader_base() = default;
            ~reader_base() = default;

            reader_base( const reader_base& ) = default;
            reader_base& operator=( const reader_base& ) = default;

            reader_base( reader_base&& ) = default;
            reader_base& operator=( reader_base&& ) = default;

            template<typename Init, typename... Args>
            reader_base( Init init_, Args... args_ ) : _src { std::forward<Init>( init_ ), std::forward<Args>( args_ )... } {}

            DataProvider& provider() { return _src; }
            const DataProvider& provider() const { return _src; }
        };

        // default version
        template<typename DataProvider, bool IsMap>
        class reader : public reader_base<DataProvider> {
        public:
            reader() = default;
            ~reader() = default;

            reader( const reader& ) = default;
            reader& operator=( const reader& ) = default;

            reader( reader&& ) = default;
            reader& operator=( reader&& ) = default;

            template<typename Init, typename... Args>
            reader( Init init_, Args... args_ ) : reader_base { std::forward<Init>( init_ ), std::forward<Args>( args_ )... } {
                _src.read( 0, sizeof( _local_header ), &_local_header );
            }

        protected:
            tga_header _local_header;

            template<typename PixelReciver>
            void read_pixels_raw( PixelReciver clb_ ) {
                const auto pix_size = ( *this )->_image_bits_per_pixel / 8;
                auto at = sizeof(tga_header)+( *this )->_id_length;
                auto to = at + ( *this )->_image_width * ( *this )->_image_height * pix_size;
                unsigned char pix;
                unsigned char buf[4];
                for ( ; at < to; ++at ) {
                    _src.read( at, at + pix_size, buf );
                    clb_( buf, buf + pix_size );
                    at += pix_size;
                }
            }

            template<typename PixelReciver>
            void read_pixels_rle( PixelReciver clb_ ) {
                const auto pix_size = ( *this )->_image_bits_per_pixel / 8;
                auto length = ( *this )->_image_width * ( *this )->_image_height;

                auto offset = sizeof(tga_header)+( *this )->_id_length;

                unsigned char pixel[4];

                size_t at = 0;
                while ( at < length ) {
                    _src.read( at, at + 1, &pixel );
                    ++at;
                    // the difference between a raw and a rle block is just
                    // when to advance after reading, a raw block advances
                    // every pixel, a rle block advances every block
                    const auto pixels = get_rle_length( pixel[0] );
                    const auto rle_pixels = is_rle( pixel[0] );
                    const auto block_advance = rle_pixels ? pix_size : 0;
                    const auto pixel_advance = rle_pixels ? 0 : pix_size;

                    if ( ( length - at ) < ( pixel_advance * pixels + block_advance ) ) {
                        break;
                    }
                    _src.read( at, at + pix_size, pixel );
                    for ( unsigned p = 0; p < pixels; ++p ) {
                        clb_( pixel, pixel + pix_size );
                        if ( pixel_advance ) {
                            at += pixel_advance;
                            _src.read( at, at + pix_size, pixel );
                        }
                    }
                    at += block_advance;
                }
            }

        public:
            const tga_header* operator->( ) const { return &_local_header; }
        };

        // memory mapped version
        template<typename DataProvider>
        class reader<DataProvider, true> : public reader_base<DataProvider> {
            const void*     _ptr { nullptr };
            size_t          _size { 0 };
        public:
            reader() = default;
            ~reader() = default;

            reader( const reader& ) = default;
            reader& operator=( const reader& ) = default;

            reader( reader&& ) = default;
            reader& operator=( reader&& ) = default;

            template<typename Init, typename... Args>
            reader( Init init_, Args... args_ ) : reader_base { std::forward<Init>( init_ ), std::forward<Args>( args_ )... } {
                _ptr = _src.data();
                _size = _src.size();
            }

        protected:
            template<typename PixelReciver>
            void read_pixels_raw( PixelReciver clb_ ) {
                auto bptr = reinterpret_cast<const unsigned char*>( _ptr );

                auto from = sizeof(tga_header)+( *this )->_id_length;
                auto to = from + ( ( *this )->_image_width * ( *this )->_image_height * ( *this )->_image_bits_per_pixel ) / 8;
                if ( to > _size ) {
                    to = _size;
                }
                clb_( bptr + from, bptr + to );
            }

            template<typename PixelReciver>
            void read_pixels_rle( PixelReciver clb_ ) {
                auto offset = sizeof(tga_header)+( *this )->_id_length;
                auto start = reinterpret_cast<const unsigned char*>( _ptr );
                auto read_range = make_iterator_range( start + offset, start + _size );
                const auto pix_size = ( *this )->_image_bits_per_pixel / 8;

                while ( !read_range.empty() ) {
                    // the difference between a raw and a rle block is just
                    // when to advance after reading, a raw block advances
                    // every pixel, a rle block advances every block
                    const auto pixels = get_rle_length( read_range.front() );
                    const auto rle_pixels = is_rle( read_range.front() );
                    const auto block_advance = rle_pixels ? pix_size : 0;
                    const auto pixel_advance = rle_pixels ? 0 : pix_size;
                    read_range.pop_front();

                    if ( read_range.size() < ( pixel_advance * pixels + block_advance ) ) {
                        break;
                    }
                    for ( unsigned p = 0; p < pixels; ++p ) {
                        clb_( read_range.begin(), read_range.begin() + pix_size );
                        read_range.advance_begin( pixel_advance );
                    }
                    read_range.advance_begin( block_advance );
                }
            }

        public:
            const tga_header* operator->( ) const { return reinterpret_cast<const tga_header*>( _ptr ); }
        };
    }


    template<typename DataProvider>
    class reader
        : public detail::reader<DataProvider, detail::has_data<DataProvider, const void*( DataProvider::* )( )const>::value> {
        typedef detail::reader<DataProvider, detail::has_data<DataProvider, const void*( DataProvider::* )( )const>::value> base_t;

    public:
        reader() = default;
        ~reader() = default;

        reader( const reader& ) = default;
        reader& operator=( const reader& ) = default;

        reader( reader&& ) = default;
        reader& operator=( reader&& ) = default;

        template<typename Init, typename... Args>
        reader( Init init_, Args... args_ )
            : base_t { std::forward<Init>( init_ ), std::forward<Args>( args_ )... } {}

        template<typename PixelReciver>
        void operator()( PixelReciver clb_ ) {
            switch ( ( *this )->_image_type ) {
            default:
            case 0: break;
            case 1: read_maped_pixels_raw<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            case 3:
            case 2: read_pixels_raw<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            case 9: read_maped_pixels_rle<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            case 11:
            case 10: read_pixels_rle<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            }
        }

        unsigned int total_pixel_count() const {
            return ( *this )->_image_width
                * ( *this )->_image_height;
        }

        unsigned int total_image_size() const {
            return total_pixel_count()
                * ( *this )->_image_bits_per_pixel
                / 8;
        }
    };

    class writer {
    public:
        template<typename PixelProvider>
        void operator()( PixelProvider clb_ );
    };
}