namespace gsdk
{
	template <typename T>
	void CVariantBase<T>::free() noexcept
	{
		if(m_flags & SV_FREE) {
			switch(m_type) {
				case FIELD_VOID: break;
				case FIELD_POSITION_VECTOR:
				case FIELD_VECTOR: {
					delete m_vector;
				} break;
				case FIELD_VECTOR2D: {
					//delete m_vector2d;
				} break;
				case FIELD_QANGLE: {
					delete m_qangle;
				} break;
				case FIELD_QUATERNION: {
					//delete m_quaternion;
				} break;
				case FIELD_UTLSTRINGTOKEN: {
					//delete m_utlstringtoken;
				} break;
				case FIELD_CSTRING: {
					std::free(m_cstr);
				} break;
				case FIELD_HSCRIPT_NEW_INSTANCE:
				case FIELD_HSCRIPT: {
					g_pScriptVM->ReleaseObject(m_object);
				} break;
				default: {
					std::free(static_cast<void *>(m_data));
				} break;
			}

			m_flags = SV_NOFLAGS;
			std::memset(m_data, 0, sizeof(m_data));
			m_type = FIELD_VOID;
		}
	}
}
