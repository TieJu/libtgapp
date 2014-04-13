#pragma once

#include <algorithm>

namespace tga {
#if defined(USE_BYTE_ORDER_TYPES)
    typedef ::byte_order::little_uchar_t    little_uchar_t;
    typedef ::byte_order::little_ushort_t   little_ushort_t;
#else
    typedef unsigned char   little_uchar_t;
    typedef unsigned short  little_ushort_t;
#endif
#pragma pack(push)
#pragma pack(1)
    struct tga_header {
        little_uchar_t  _id_length;
        little_uchar_t  _color_map_type;
        little_uchar_t  _image_type;
        little_ushort_t _color_map_first_entry_index;
        little_ushort_t _color_map_length;
        little_uchar_t  _color_map_entry_size;
        little_ushort_t _image_x_origin;
        little_ushort_t _image_y_origin;
        little_ushort_t _image_width;
        little_ushort_t _image_height;
        little_uchar_t  _image_bits_per_pixel;
        little_uchar_t  _image_descriptor;
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

        template<typename DataProvider, typename Derived>
        class reader_base {
        protected:
            DataProvider    _src;

            inline static bool is_rle( unsigned char byte_ ) { return ( ( (byte_)& 0x80 ) != 0 ); }
            inline static unsigned get_rle_length( unsigned char byte_ ) { return ( 1 + ( (byte_)& 0x7F ) ); }

            template<typename PixelReciver>
            void read_maped_pixels_rle( PixelReciver clb_ ) {}

            template<typename PixelReciver>
            void read_maped_pixels_raw( PixelReciver clb_ ) {}

            template<typename PixelReciver>
            void read_mono_pixels_rle( PixelReciver clb_ ) {}

            template<typename PixelReciver>
            void read_mono_pixels_raw( PixelReciver clb_ ) {}

            size_t color_map_offset() {
                return sizeof(tga_header) + header()._id_length;
            }

            size_t color_map_size() {
                return header()._color_map_entry_size
                     * header()._color_map_length
                     / 8;
            }

            size_t image_map_offset() {
                return color_map_offset()
                     + color_map_size()
            }

            size_t image_map_size() {
                return total_image_size();
            }

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

            const tga_header& header() const { return static_cast<const Derived*>( this )->get_header(); }

            unsigned int total_pixel_count() const {
                return  header()._image_width
                     *  header()._image_height;
            }

            unsigned int total_image_bits() const {
                return total_pixel_count()
                     *  pixel_bits();
            }

            unsigned int total_image_size() const {
                return total_image_bits() / 8;
            }

            unsigned int pixel_bits() const {
                return header()._image_bits_per_pixel;
            }

            unsigned int pixel_bytes() const {
                return pixel_bits() / 8;
            }
        };

        // default version
        template<typename DataProvider, bool IsMap>
        class reader : public reader_base<DataProvider, reader<DataProvider, IsMap>> {
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
            friend class reader_base<DataProvider, reader<DataProvider, IsMap>>;
            tga_header _local_header;
            const tga_header& get_header() const { return _local_header; }

            template<typename PixelReciver>
            void read_pixels_raw( PixelReciver clb_ ) {
                const auto pix_size = pixel_bytes();
                auto at = image_map_offset();
                auto to = at + image_map_size();
                unsigned char buf[4];
                for ( ; at < to; ++at ) {
                    _src.read( at, at + pix_size, buf );
                    clb_( buf, buf + pix_size );
                    at += pix_size;
                }
            }

            template<typename PixelReciver>
            void read_pixels_rle( PixelReciver clb_ ) {
                auto from = image_map_offset();
                const auto to = from + image_map_size();
                const auto pixel_size = pixel_bytes();
                unsigned char buf[4];

                while ( from < to ) {
                    _src.read( from, from + 1, buf );
                    ++from;
                    // the difference between a raw and a rle block is just
                    // when to advance after reading, a raw block advances
                    // every pixel, a rle block advances every block
                    const auto is_rle_block = is_rle( buf[0] );
                    const auto pixel_count = get_rle_length( buf[0] );
                    const auto block_advance = is_rle_block ? pixel_size : 0;
                    const auto pixel_advance = is_rle_block ? 0 : pixel_size;
                    if ( from + block_advance + pixel_advance * pixel_count > to ) {
                        break;
                    }

                    _src.read( from, from + pixel_size, buf );
                    for ( unsigned p = 0; p < pixel_count; ++p ) {
                        clb_( buf, buf + pixel_size );
                        if ( pixel_advance ) {
                            at += pixel_advance;
                            _src.read( from, from + pixel_size, buf );
                        }
                    }
                    from += block_advance;
                }
            }
        };

        // memory mapped version
        template<typename DataProvider>
        class reader<DataProvider, true>
            : public reader_base<DataProvider, reader<DataProvider, true>> {
            const void*     _ptr { nullptr };
            size_t          _size { 0 };

            friend class reader_base<DataProvider, reader<DataProvider, true>>;
            const tga_header& get_header() const { return *reinterpret_cast<const tga_header*>( _ptr ); }
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

                auto from = image_map_offset();
                auto to = from + total_image_size();
                if ( to > _size ) {
                    to = _size;
                }
                clb_( bptr + from, bptr + to );
            }

            template<typename PixelReciver>
            void read_pixels_rle( PixelReciver clb_ ) {
                auto offset = image_map_offset();
                auto start = reinterpret_cast<const unsigned char*>( _ptr );
                auto read_range = make_iterator_range( start + offset, start + _size );
                const auto pix_size = pixel_bytes();

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
            switch ( header()._image_type ) {
            default:
            case 0: break;
            case 1: read_maped_pixels_raw<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            case 3: read_mono_pixels_raw<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            case 2: read_pixels_raw<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            case 9: read_maped_pixels_rle<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            case 10: read_pixels_rle<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            case 11: read_mono_pixels_rle<PixelReciver>( std::forward<PixelReciver>( clb_ ) ); break;
            }
        }
    };

    template<typename DataReciver>
    class writer {
    public:
        enum class compression_mode { none, rle };
        void size( unsigned width_, unsigned height_, unsigned bits_per_pixel_ );
        void flag( compression_mode compression_ );
        template<typename PixelProvider>
        void operator()( PixelProvider clb_ ) {
            write_header();

            auto byte_c = _w * _h * _bpp;
            _written_bytes = 0;
            while ( _written_bytes < byte_c ) {
                auto range = clb_();
                while ( !range.empty() && byte_c < pix_c ) {
                    range = next_pixels( range );
                }
            }
            // ensure last block is stored
            store_pixel_block(true);
        }

    private:
        compression_mode    _comp { compression_mode::none };
        unsigned            _w { 0 }, _h { 0 }, _bpp { 0 };
        unsigned            _block_size { 0 };
        unsigned char       _block[128*8];
        unsigned            _written_bytes;
        DataReciver         _dst;

        void write_header() {
            tga_header header;
            header._id_length = 0;
            header._color_map_type = 0;
            header._image_type = _comp == compression_mode::none ? 2 : 10 ;
            header._color_map_first_entry_index = 0;
            header._color_map_length = 0;
            header._color_map_entry_size = 0;
            header._image_x_origin = 0;
            header._image_y_origin = 0;
            header._image_width = _w;
            header._image_height = _h;
            header._image_bits_per_pixel = _bpp * 8;
            header._image_descriptor = _bpp == 4 ? 8 : 0;
            _dst( &header, &header + 1 );
        }

        unsigned write_raw_block( unsigned offset_, unsigned length_ ) {
            auto end = offset_ + length_ * _bpp;
            _dst( _block + offset_, _block + end );
            return end;
        }

        unsigned write_rle_block( unsigned offset_, unsigned length_ ) {
            auto end = offset_ + _bpp;
            auto start = _block + offset_;
            auto stop = _block + end;
            for ( unsigned rep = 0; rep < length_; ++rep ) {
                _dst( start, stop );
            }
            return end;
        }

        void store_pixel_block( bool last_block_ = false ) {
            if ( !_pixel_block_size ) {
                return;
            }
            if ( _comp == compression_mode::none ) {
                _dst.write( _pixel_block, _pixel_block + _pixel_block_size * _bpp );
            } else {
                unsigned offset = 0;
                unsigned rle_len = 0;
                unsigned raw_len = 0;
                unsigned pixel = 0;
                for ( ; pixel < _pixel_block_size; pixel += _bpp ) {
                    if ( std::equal( _block + pixel, _block + pixel + _bpp, _block + pixel ) ) {
                        if ( raw_len ) {
                            offset = write_raw_block( offset, raw_len );
                            raw_len = 0;
                        } else {
                            ++rle_len;
                        }
                    } else {
                        if ( rle_len ) {
                            offset = write_rle_block( offset, rle_len );
                            rle_len = 0;
                        } else {
                            ++raw_len;
                        }
                    }

                    if ( raw_len == 128 ) {
                        offset = write_raw_block( offset, raw_len );
                        raw_len = 0;
                    } else if ( rle_len == 128 ) {
                        offset = write_rle_block( offset, rle_len );
                        rle_len = 0;
                    }
                }
                if ( last_block_ ) {
                    if ( raw_len ) {
                        write_raw_block( offset, raw_len );
                    } else if ( rle_len ) {
                        write_rle_block( offset, rle_len );
                    }
                    _block_size = 0;
                } else if ( offset < _block_size ) {
                    std::copy( _block + offset, _block + _block_size, _block );
                    _block_size -= offset;
                }
            }
        }

        template<typename Range>
        Range next_pixels( Range pixel_ ) {
            while ( !pixel_.empty() ) {
                pixel_ = fill_pixel_block( pixel_ );
            }
        }

        template<typename Range>
        Range fill_pixel_block( Range pixel_ ) {
            if ( _block_size == sizeof( _block ) ) {
                store_pixel_block();
            }
            while ( _block_size < sizeof( _block ) && !pixel_.empty() ) {
                _block[_block_size++] = pixel_.front();
                pixel_.pop_front();
                ++_written_bytes;
            }
            return pixel_;
        }
    };
}